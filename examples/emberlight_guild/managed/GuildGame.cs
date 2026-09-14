using Vespera;

namespace EmberlightGuild;

public sealed class GuildGame : Component
{
    private sealed record Adventurer(string Id, string Name, string Role, int Level, int Might, int Wit, int Spirit);
    private sealed record Mission(string Id, string Name, int Danger, int Gold, int Renown, float Duration, string Description);

    private static readonly Adventurer[] Adventurers =
    {
        new("member-aldric", "Aldric", "Vanguard", 4, 8, 3, 5),
        new("member-mira", "Mira", "Arcanist", 3, 2, 9, 7),
        new("member-tamsin", "Tamsin", "Ranger", 3, 6, 5, 7),
        new("member-oren", "Brother Oren", "Healer", 2, 3, 7, 9),
        new("member-nix", "Nix", "Scout", 2, 5, 8, 5),
    };

    private static readonly Mission[] Missions =
    {
        new("mission-tollhouse", "Goblin Tollhouse", 3, 45, 7, 3.5f, "Clear a tollhouse seized by goblins before the road closes for the night."),
        new("mission-moonwell", "Moonwell Escort", 5, 70, 12, 5.0f, "Escort a scholar through the old moonwell road and bring the party home safely."),
        new("mission-crypt", "Crypt of Whispers", 7, 110, 18, 6.5f, "Investigate a sealed crypt where the guild's scouts keep hearing voices below the stone."),
    };

    private readonly Dictionary<string, UiElement> _memberButtons = new(StringComparer.Ordinal);
    private readonly Dictionary<string, UiElement> _missionButtons = new(StringComparer.Ordinal);
    private UiElement _missionTitle = null!;
    private UiElement _missionMeta = null!;
    private UiElement _missionDescription = null!;
    private UiElement _missionStatus = null!;
    private UiElement _progressFill = null!;
    private UiElement _startMission = null!;
    private UiElement _upgrade = null!;
    private UiElement _eventLog = null!;
    private UiElement _guildName = null!;
    private UiElement _goldText = null!;
    private UiElement _renownText = null!;
    private UiElement _rankText = null!;
    private UiElement _memberRole = null!;
    private UiElement _memberState = null!;
    private UiElement _might = null!;
    private UiElement _wit = null!;
    private UiElement _spirit = null!;

    private Adventurer _selectedAdventurer = Adventurers[0];
    private Mission _selectedMission = Missions[1];
    private bool _missionActive;
    private float _missionElapsed;
    private int _missionSerial;
    private int _gold = 320;
    private int _renown = 18;
    private int _warRoomTier = 1;

    public override void Start()
    {
        foreach (var adventurer in Adventurers)
            _memberButtons[adventurer.Id] = UI.FindRequired(adventurer.Id);
        foreach (var mission in Missions)
            _missionButtons[mission.Id] = UI.FindRequired(mission.Id);

        _missionTitle = UI.FindRequired("mission-title");
        _missionMeta = UI.FindRequired("mission-meta");
        _missionDescription = UI.FindRequired("mission-description");
        _missionStatus = UI.FindRequired("mission-status");
        _progressFill = UI.FindRequired("progress-fill");
        _startMission = UI.FindRequired("start-mission");
        _upgrade = UI.FindRequired("upgrade");
        _eventLog = UI.FindRequired("event-log");
        _guildName = UI.FindRequired("guild-name");
        _goldText = UI.FindRequired("gold");
        _renownText = UI.FindRequired("renown");
        _rankText = UI.FindRequired("rank");
        _memberRole = UI.FindRequired("member-role");
        _memberState = UI.FindRequired("member-state");
        _might = UI.FindRequired("stat-might");
        _wit = UI.FindRequired("stat-wit");
        _spirit = UI.FindRequired("stat-spirit");

        SaveData.UseSlot("emberlight-guild");
        if (SaveData.Load())
        {
            _gold = SaveData.GetInt("gold", _gold);
            _renown = SaveData.GetInt("renown", _renown);
            _warRoomTier = Math.Max(1, SaveData.GetInt("war_room_tier", _warRoomTier));
            _guildName.ValueText = SaveData.GetString("guild_name", "Emberlight Guild");
            _missionSerial = SaveData.GetInt("mission_serial", 0);
        }

        SelectAdventurer(_selectedAdventurer);
        SelectMission(_selectedMission);
        RefreshResources();
        RefreshUpgradeButton();
        Log.Info("Emberlight Guild sample started through project-owned C# + RmlUi.");
    }

    public override void Update(float deltaTime)
    {
        foreach (var adventurer in Adventurers)
            if (_memberButtons[adventurer.Id].Clicked) SelectAdventurer(adventurer);

        foreach (var mission in Missions)
            if (_missionButtons[mission.Id].Clicked && !_missionActive) SelectMission(mission);

        if (_upgrade.Clicked && !_missionActive)
            UpgradeWarRoom();

        if (_startMission.Clicked && !_missionActive)
            StartMission();

        if (!_missionActive) return;
        _missionElapsed += Math.Max(0.0f, deltaTime);
        var progress = Math.Clamp(_missionElapsed / _selectedMission.Duration, 0.0f, 1.0f);
        _progressFill.SetProperty("width", $"{progress * 100.0f:0.0}%");
        _missionStatus.Text = $"Expedition underway · {Math.Max(0.0f, _selectedMission.Duration - _missionElapsed):0.0}s remaining";
        if (_missionElapsed >= _selectedMission.Duration) ResolveMission();
    }

    public override void OnDestroy() => Persist();

    private void SelectAdventurer(Adventurer selected)
    {
        _selectedAdventurer = selected;
        foreach (var adventurer in Adventurers)
            _memberButtons[adventurer.Id].SetClass("selected", adventurer.Id == selected.Id);
        _memberRole.Text = $"{selected.Name.ToUpperInvariant()} · {selected.Role.ToUpperInvariant()}";
        _memberState.Text = $"READY · LEVEL {selected.Level}";
        _might.Text = selected.Might.ToString();
        _wit.Text = selected.Wit.ToString();
        _spirit.Text = selected.Spirit.ToString();
    }

    private void SelectMission(Mission selected)
    {
        _selectedMission = selected;
        foreach (var mission in Missions)
            _missionButtons[mission.Id].SetClass("selected", mission.Id == selected.Id);
        _missionTitle.Text = selected.Name;
        _missionMeta.Text = $"Danger {selected.Danger} · Reward {selected.Gold} gold / {selected.Renown} renown · ~{selected.Duration:0.#} seconds";
        _missionDescription.Text = selected.Description;
        _missionStatus.Text = "Ready to assign a party.";
        _progressFill.SetProperty("width", "0%");
    }

    private void StartMission()
    {
        _missionActive = true;
        _missionElapsed = 0.0f;
        _missionSerial++;
        _startMission.Text = "Expedition in Progress";
        _eventLog.Text = $"RECENT EVENTS — {_selectedAdventurer.Name}'s party departed for {_selectedMission.Name}.";
    }

    private void ResolveMission()
    {
        _missionActive = false;
        var victory = (_missionSerial % 4) != 0;
        _progressFill.SetProperty("width", "0%");
        _startMission.Text = "Start Expedition";
        if (victory)
        {
            _gold += _selectedMission.Gold;
            _renown += _selectedMission.Renown;
            _missionStatus.Text = "Victory · party returned safely.";
            _eventLog.Text = $"RECENT EVENTS — Victory at {_selectedMission.Name} · +{_selectedMission.Gold} gold · +{_selectedMission.Renown} renown.";
        }
        else
        {
            _missionStatus.Text = "Setback · the party returned bruised.";
            _eventLog.Text = $"RECENT EVENTS — {_selectedMission.Name} went badly, but the guild survived the contract.";
        }
        RefreshResources();
        Persist();
    }

    private void UpgradeWarRoom()
    {
        var cost = UpgradeCost();
        if (_gold < cost)
        {
            _eventLog.Text = $"RECENT EVENTS — War Room upgrade needs {cost} gold.";
            return;
        }
        _gold -= cost;
        _warRoomTier++;
        _eventLog.Text = $"RECENT EVENTS — War Room upgraded to tier {_warRoomTier}.";
        RefreshResources();
        RefreshUpgradeButton();
        Persist();
    }

    private int UpgradeCost() => 100 + ((_warRoomTier - 1) * 75);

    private void RefreshUpgradeButton() => _upgrade.Text = $"Upgrade War Room · {UpgradeCost()}g";

    private void RefreshResources()
    {
        _goldText.Text = _gold.ToString();
        _renownText.Text = _renown.ToString();
        _rankText.Text = _renown switch
        {
            >= 100 => "GOLD I",
            >= 60 => "SILVER I",
            >= 30 => "BRONZE I",
            _ => "BRONZE II",
        };
    }

    private void Persist()
    {
        if (_guildName is not null)
            SaveData.SetString("guild_name", _guildName.ValueText);
        SaveData.SetInt("gold", _gold);
        SaveData.SetInt("renown", _renown);
        SaveData.SetInt("war_room_tier", _warRoomTier);
        SaveData.SetInt("mission_serial", _missionSerial);
        try { SaveData.Save(); }
        catch (Exception ex) { Log.Warning($"Emberlight save failed: {ex.Message}"); }
    }
}
