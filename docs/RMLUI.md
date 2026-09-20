# RmlUi

RmlUi is Vespera's production runtime UI path. Use `.rml` for document structure and `.rcss` for presentation. A project can designate a startup RML document that loads in Editor Play and exported games.

## Create the files

A simple project layout:

```text
assets/ui/
├─ main.rml
└─ theme.rcss
```

Example RML:

```html
<rml>
<head>
  <link type="text/rcss" href="theme.rcss" />
</head>
<body>
  <div id="hud">
    <span id="score">Score 0</span>
    <button id="reset">Reset</button>
  </div>
</body>
</rml>
```

Give elements an `id` when gameplay code needs to find them.

## Set the startup UI

Select the `.rml` document in Project / Assets. In **Project Settings → Startup & Runtime**, choose **Use Selected RML** for Startup RML UI.

The project stores both a stable asset ID and a fallback path for the startup document.

## Drive RmlUi from C#

```csharp
private UiElement? _score;
private UiElement? _reset;

public override void Start()
{
    _score = UI.Find("score");
    _reset = UI.Find("reset");
}

public override void Update(float dt)
{
    if (_reset?.Clicked ?? false)
        ResetGame();
}
```

Change text, classes, and CSS properties through the public UI abstraction:

```csharp
_score!.Text = "Score 12";
_score.SetClass("hot", true);
_score.SetProperty("opacity", "0.85");
```

## Referencing images, fonts, and style sheets

RML/RCSS files can reference project images/fonts/style sheets by project-relative paths. The asset authoring layer understands RML/RCSS dependencies and can rewrite supported path references during controlled asset moves.

## Input and focus

Runtime UI participates in pointer, keyboard navigation, text-input, focus, and click state. `UiElement.Clicked` consumes a queued click once, making polling deterministic from gameplay code.

## Legacy `.slui`

Vespera can still load legacy `.slui` documents for compatibility/tooling. New production UI should use RmlUi unless you specifically need the legacy authoring surface.
