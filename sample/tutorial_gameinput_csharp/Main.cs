using Godot;
using GodotGameInput;
using TutorialGameInputCSharp.SelfTest;

namespace TutorialGameInputCSharp;

/// <summary>
/// GameInput action-bridge tutorial (C# port of <c>sample/tutorial_gameinput/main.gd</c>).
///
/// Builds a <see cref="GameInputActionMap"/> programmatically, attaches a
/// <see cref="GameInputMapper"/> that polls every frame and drives Godot's
/// <c>InputMap</c>, and renders live action state plus device hot-plug events.
/// Jumping sends a short rumble to the gamepad (Step 7).
///
/// Run with <c>-- --gameinput-selftest</c> to check the managed facade's
/// integration automatically instead (see <c>SelfTest/GameInputSelfTest.cs</c>).
///
/// Independent of GDK / PlayFab — no sign-in flow.
/// </summary>
public partial class Main : Control
{
    private const float PlayerSpeed = 240.0f;
    private const float PlayerJumpVelocity = -480.0f;
    private const float PlayerGravity = 1200.0f;

    // Step 7: a short thump on the gamepad when the player jumps.
    public const float JumpRumbleWeak = 0.2f;
    public const float JumpRumbleStrong = 0.4f;
    public const float JumpRumbleSec = 0.12f;

    private Label _runtimeStatus;
    private Label _deviceCount;
    private Label _devices;
    private Label _actionState;
    private RichTextLabel _hotplugLog;
    private ColorRect _floor;
    private ColorRect _player;

    private float _playerVelocityY;
    private GameInputMapper _mapper;
    private bool _subscribed;

    public override void _Ready()
    {
        if (GameInputSelfTest.IsRequested())
        {
            // Parented to the root, not this scene: one check takes this scene out of the tree.
            var selfTest = new GameInputSelfTest { Name = "SelfTest", Sample = this };
            GetTree().Root.CallDeferred(Node.MethodName.AddChild, selfTest);
        }

        _runtimeStatus = GetNode<Label>("Root/RuntimeStatus");
        _deviceCount = GetNode<Label>("Root/DeviceCount");
        _devices = GetNode<Label>("Root/Devices");
        _actionState = GetNode<Label>("Root/ActionState");
        _hotplugLog = GetNode<RichTextLabel>("Root/HotplugLog");
        _floor = GetNode<ColorRect>("Floor");
        _player = GetNode<ColorRect>("Player");

        if (!GameInput.IsAvailable)
        {
            _runtimeStatus.Text = "GameInput singleton missing. Build the addon (cmake --build build --preset debug).";
            _deviceCount.Text = "";
            _devices.Text = "";
            _actionState.Text = "";
            return;
        }

        if (!GameInput.IsInitialized)
        {
            GD.PushWarning("[Pad] GameInput runtime not available — gamepad input disabled.");
            _runtimeStatus.Text = "GameInput runtime NOT initialized (set game_input/runtime/initialize_on_startup=true).";
        }
        else
        {
            _runtimeStatus.Text = "GameInput runtime initialized.";
        }

        _mapper = GameInputMapper.Create();
        _mapper.Node.Name = "GamepadMapper";
        _mapper.ActionMap = BuildDefaultMap();
        AddChild(_mapper.Node);

        Subscribe();

        RefreshDevices();
        AppendHotplug($"Seeded with {GameInput.GetConnectedDeviceCount(GameInput.DeviceKind.Gamepad)} gamepad(s) at startup");

        _player.Position = new Vector2(PlayAreaWidth * 0.5f, FloorY);
    }

    // GameInput's C# events are static, so they would keep this node alive and
    // call into it after it is freed. Subscribe while in the tree only.
    public override void _EnterTree()
    {
        if (_mapper != null)
        {
            Subscribe();
        }
    }

    public override void _ExitTree()
    {
        if (_subscribed)
        {
            GameInput.DeviceConnected -= OnDeviceConnected;
            GameInput.DeviceDisconnected -= OnDeviceDisconnected;
            _subscribed = false;
        }
    }

    private void Subscribe()
    {
        if (!_subscribed)
        {
            GameInput.DeviceConnected += OnDeviceConnected;
            GameInput.DeviceDisconnected += OnDeviceDisconnected;
            _subscribed = true;
        }
    }

    private static GameInputActionMap BuildDefaultMap()
    {
        GameInputActionMap map = GameInputActionMap.Create();

        GameInputBinding accept = GameInputBinding.Create();
        accept.Action = "ui_accept";
        accept.Source = GameInputDevice.Source.BtnA;
        map.AddBinding(accept);

        GameInputBinding jump = GameInputBinding.Create();
        jump.Action = "jump";
        jump.Source = GameInputDevice.Source.BtnA;
        map.AddBinding(jump);

        GameInputBinding left = GameInputBinding.Create();
        left.Action = "move_left";
        left.Source = GameInputDevice.Source.AxisLeftX;
        left.IsAxis = true;
        left.AxisInvert = true;
        map.AddBinding(left);

        GameInputBinding right = GameInputBinding.Create();
        right.Action = "move_right";
        right.Source = GameInputDevice.Source.AxisLeftX;
        right.IsAxis = true;
        map.AddBinding(right);

        return map;
    }

    public override void _PhysicsProcess(double delta)
    {
        if (!GameInput.IsAvailable)
        {
            return;
        }

        float direction = Input.GetActionStrength("move_right") - Input.GetActionStrength("move_left");
        Vector2 position = _player.Position;
        position.X += direction * PlayerSpeed * (float)delta;

        if (Input.IsActionJustPressed("jump") && position.Y >= FloorY)
        {
            _playerVelocityY = PlayerJumpVelocity;
            RumblePad();
        }

        _playerVelocityY += PlayerGravity * (float)delta;
        position.Y += _playerVelocityY * (float)delta;
        if (position.Y >= FloorY)
        {
            position.Y = FloorY;
            _playerVelocityY = 0.0f;
        }

        position.X = Mathf.Clamp(position.X, 0.0f, PlayerMaxX);
        _player.Position = position;
    }

    public override void _Process(double delta)
    {
        if (!GameInput.IsAvailable)
        {
            return;
        }

        _actionState.Text =
            $"move_left={Input.GetActionStrength("move_left"):0.00}  " +
            $"move_right={Input.GetActionStrength("move_right"):0.00}  " +
            $"jump={Input.IsActionPressed("jump")}  " +
            $"ui_accept={Input.IsActionPressed("ui_accept")}";
    }

    private void OnDeviceConnected(GameInputDevice device)
    {
        HotplugEventsHandled++;
        AppendHotplug($"connected: id={device.DeviceId} ({device.DisplayName})");
        RefreshDevices();
    }

    private void OnDeviceDisconnected(long deviceId)
    {
        HotplugEventsHandled++;
        AppendHotplug($"disconnected: id={deviceId}");
        RefreshDevices();
    }

    private void RefreshDevices()
    {
        _deviceCount.Text = $"Connected gamepads: {GameInput.GetConnectedDeviceCount(GameInput.DeviceKind.Gamepad)}";

        var lines = new System.Collections.Generic.List<string>();
        foreach (GameInputDevice device in GameInput.GetDevices(GameInput.DeviceKind.Gamepad))
        {
            lines.Add($"- id={device.DeviceId} {device.DisplayName}");
        }

        if (lines.Count == 0)
        {
            lines.Add("- (none — plug in a gamepad)");
        }

        _devices.Text = string.Join("\n", lines);
    }

    private void AppendHotplug(string line) => _hotplugLog.AppendText(line + "\n");

    // Step 7: rumble the gamepad that drives the mapper. The duration makes
    // GameInput.Poll() stop the motors, so there is no timer to manage here.
    private void RumblePad()
    {
        GameInputDevice pad = MapperDevice();
        if (pad != null && pad.SupportsVibration)
        {
            pad.StartVibration(JumpRumbleWeak, JumpRumbleStrong, JumpRumbleSec);
        }
    }

    private GameInputDevice MapperDevice()
    {
        if (_mapper == null || !GameInput.IsInitialized)
        {
            return null;
        }

        return _mapper.TargetDeviceId >= 0
            ? GameInput.GetDeviceById(_mapper.TargetDeviceId)
            : GameInput.GetPrimaryDevice(GameInput.DeviceKind.Gamepad);
    }

    // Read-only views for SelfTest/GameInputSelfTest.cs.

    public GameInputMapper Mapper => _mapper;

    public string DevicesText => _devices.Text;

    public string DeviceCountText => _deviceCount.Text;

    public string HotplugText => _hotplugLog.GetParsedText();

    /// <summary>Hot-plug events this scene has handled; stops counting once it leaves the tree.</summary>
    public int HotplugEventsHandled { get; private set; }

    public bool IsPlayerOnFloor => _player.Position.Y >= FloorY;

    public Vector2 PlayerPosition => _player.Position;

    public float PlayerMaxX => PlayAreaWidth - _player.Size.X;

    private float PlayAreaWidth => GetViewportRect().Size.X;

    // The player stands on the Floor line under the text column.
    private float FloorY => _floor.Position.Y - _player.Size.Y;
}
