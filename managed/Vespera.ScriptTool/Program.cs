using System.Globalization;
using System.Reflection;
using System.Runtime.Loader;
using System.Text;
using Vespera;

static string Quote(string value)
{
    var builder = new StringBuilder(value.Length + 2);
    builder.Append('"');
    foreach (var ch in value)
    {
        switch (ch)
        {
            case '\\': builder.Append("\\\\"); break;
            case '"': builder.Append("\\\""); break;
            case '\n': builder.Append("\\n"); break;
            case '\r': builder.Append("\\r"); break;
            case '\t': builder.Append("\\t"); break;
            default: builder.Append(ch); break;
        }
    }
    builder.Append('"');
    return builder.ToString();
}

static string Humanize(string name)
{
    if (string.IsNullOrEmpty(name)) return name;
    var builder = new StringBuilder(name.Length + 8);
    for (var i = 0; i < name.Length; ++i)
    {
        var ch = name[i];
        if (i > 0 && char.IsUpper(ch) && (char.IsLower(name[i - 1]) || char.IsDigit(name[i - 1])))
            builder.Append(' ');
        builder.Append(ch);
    }
    return builder.ToString();
}

static string CanonicalType(Type type)
{
    if (type == typeof(bool)) return "bool";
    if (type == typeof(int)) return "int";
    if (type == typeof(float)) return "float";
    if (type == typeof(string)) return "string";
    if (type.IsEnum) return "enum";
    if (type == typeof(Vector2)) return "vec2";
    if (type == typeof(Vector3)) return "vec3";
    if (type == typeof(Color)) return "color";
    return "unsupported";
}

static FieldInfo[] ExposedFields(Type type) => type
    .GetFields(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic)
    .Where(field => field.GetCustomAttribute<ExposeAttribute>(inherit: true) is not null)
    .OrderBy(field => field.MetadataToken)
    .ToArray();

if (args.Length != 2)
{
    Console.Error.WriteLine("Usage: Vespera.ScriptTool <GameScripts.dll> <output metadata file>");
    return 2;
}

var assemblyPath = Path.GetFullPath(args[0]);
var outputPath = Path.GetFullPath(args[1]);
if (!File.Exists(assemblyPath))
{
    Console.Error.WriteLine($"Game script assembly not found: {assemblyPath}");
    return 3;
}

try
{
    var assembly = AssemblyLoadContext.Default.LoadFromAssemblyPath(assemblyPath);
    var scriptTypes = assembly.GetTypes()
        .Where(type => !type.IsAbstract && type != typeof(Component) && typeof(Component).IsAssignableFrom(type))
        .OrderBy(type => type.FullName, StringComparer.Ordinal)
        .ToArray();

    // 0.5.5 treats reflection metadata generation as a build contract check, not
    // merely a best-effort listing pass. Bad [Expose] declarations and component
    // shapes fail before last-good output is replaced, so the editor/runtime can
    // keep using the previous known-good managed build.
    var errors = new List<string>();
    foreach (var type in scriptTypes)
    {
        var typeName = type.FullName ?? type.Name;
        if (type.ContainsGenericParameters)
            errors.Add($"error VESPERA1001: C# component '{typeName}' must be a closed, non-generic type.");

        var constructor = type.GetConstructor(
            BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic,
            null, Type.EmptyTypes, null);
        if (constructor is null)
            errors.Add($"error VESPERA1002: C# component '{typeName}' needs a parameterless constructor so Vespera can create it.");

        var serializedKeys = new Dictionary<string, string>(StringComparer.Ordinal);
        foreach (var field in ExposedFields(type))
        {
            var fieldName = $"{typeName}.{field.Name}";
            var canonical = CanonicalType(field.FieldType);
            if (canonical == "unsupported")
                errors.Add($"error VESPERA1101: [Expose] field '{fieldName}' uses unsupported type '{field.FieldType.FullName ?? field.FieldType.Name}'. Supported 0.5.x types are bool, int, float, string, enum, Vector2, Vector3, and Color.");
            if (field.IsInitOnly)
                errors.Add($"error VESPERA1102: [Expose] field '{fieldName}' is readonly. Exposed fields must be writable so serialized Inspector values can be applied before Start().");

            var range = field.GetCustomAttribute<RangeAttribute>(inherit: true);
            if (range is not null && field.FieldType != typeof(float) && field.FieldType != typeof(int))
                errors.Add($"error VESPERA1103: [Range] on '{fieldName}' is only valid for int or float exposed fields.");

            var aliases = field.GetCustomAttributes<FormerlySerializedAsAttribute>(inherit: true)
                .Select(value => value.Name)
                .Where(value => !string.IsNullOrWhiteSpace(value))
                .Distinct(StringComparer.Ordinal)
                .ToArray();
            foreach (var key in new[] { field.Name }.Concat(aliases))
            {
                if (serializedKeys.TryGetValue(key, out var owner) && !string.Equals(owner, field.Name, StringComparison.Ordinal))
                    errors.Add($"error VESPERA1104: C# component '{typeName}' maps serialized field key '{key}' to both '{owner}' and '{field.Name}'. Current names and [FormerlySerializedAs] aliases must be unique within a component.");
                else
                    serializedKeys[key] = field.Name;
            }
        }
    }

    if (errors.Count > 0)
    {
        foreach (var error in errors.Distinct(StringComparer.Ordinal)) Console.Error.WriteLine(error);
        return 6;
    }

    Directory.CreateDirectory(Path.GetDirectoryName(outputPath)!);
    using var writer = new StreamWriter(outputPath, false, new UTF8Encoding(encoderShouldEmitUTF8Identifier: false));
    writer.WriteLine("vespera_script_metadata 4");
    writer.WriteLine($"assembly {Quote(assembly.GetName().Name ?? Path.GetFileNameWithoutExtension(assemblyPath))}");

    foreach (var type in scriptTypes)
    {
        var fullName = type.FullName ?? type.Name;
        writer.WriteLine($"script {Quote(fullName)}");
        foreach (var field in ExposedFields(type))
        {
            var expose = field.GetCustomAttribute<ExposeAttribute>(inherit: true)!;
            var displayName = string.IsNullOrWhiteSpace(expose.DisplayName) ? Humanize(field.Name) : expose.DisplayName!;
            var canonical = CanonicalType(field.FieldType);
            var tooltip = field.GetCustomAttribute<TooltipAttribute>(inherit: true)?.Text ?? string.Empty;
            var range = field.GetCustomAttribute<RangeAttribute>(inherit: true);
            var hasRange = range is not null && (field.FieldType == typeof(float) || field.FieldType == typeof(int));
            var min = hasRange ? range!.Min.ToString("R", CultureInfo.InvariantCulture) : "0";
            var max = hasRange ? range!.Max.ToString("R", CultureInfo.InvariantCulture) : "0";
            writer.WriteLine($"  field {Quote(field.Name)} {Quote(canonical)} {Quote(displayName)} {Quote(field.FieldType.FullName ?? field.FieldType.Name)} {Quote(tooltip)} {(hasRange ? 1 : 0)} {min} {max}");
            if (field.FieldType.IsEnum)
            {
                foreach (var name in Enum.GetNames(field.FieldType))
                    writer.WriteLine($"    enum_value {Quote(name)}");
            }
            foreach (var alias in field.GetCustomAttributes<FormerlySerializedAsAttribute>(inherit: true)
                         .Select(value => value.Name)
                         .Where(value => !string.IsNullOrWhiteSpace(value))
                         .Distinct(StringComparer.Ordinal))
            {
                writer.WriteLine($"    alias {Quote(alias)}");
            }
        }
        writer.WriteLine("endscript");
    }
    writer.WriteLine("end_metadata");
    Console.WriteLine($"Wrote validated metadata for {scriptTypes.Length} Vespera C# component(s): {outputPath}");
    return 0;
}
catch (ReflectionTypeLoadException ex)
{
    Console.Error.WriteLine("Could not reflect game scripts:");
    foreach (var loader in ex.LoaderExceptions.Where(e => e is not null)) Console.Error.WriteLine(loader!.Message);
    return 4;
}
catch (Exception ex)
{
    Console.Error.WriteLine($"C# metadata generation failed: {ex}");
    return 5;
}
