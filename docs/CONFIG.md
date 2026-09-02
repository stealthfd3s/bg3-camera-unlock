# Configuration reference

Every option, what it does, and how to tune it.

The file lives at:

```
~/Documents/BG3 Camera Unlock/config.ini
```

It is created on first launch. **Quit BG3 before editing and restart it
afterwards** — settings are read once, at injection time. There is no live
reload.

The file is deliberately outside the `.app` bundle: editing it does not
modify the application or invalidate its code signature.

---

## How bad input is handled

The parser never lets a broken config reach the game.

- An **out-of-range** value is rejected; the default is kept and the log says
  which line and what was expected.
- An **unparseable** value (`VerticalSensitivity=banana`) is rejected the
  same way.
- An **unknown key** is ignored and logged.
- **Contradictory pairs** — a pitch minimum above the maximum, or a zoom
  maximum below the minimum — cause *both* values in the pair to revert to
  their defaults, so you never get half a setting.
- A **missing or unreadable file** falls back to defaults entirely.

Nothing here can crash the game. Check `mod.log` after editing to confirm
what was actually loaded — the parser logs the full resolved configuration
on every launch.

---

## `ConfigVersion`

```ini
ConfigVersion=1
```

Schema version of the file. **Leave it alone** unless a release note tells
you otherwise.

It exists so a future release that has to *rename* a key can migrate your
settings instead of silently resetting them. Adding new keys does not bump
it — unknown keys are already ignored and missing keys already fall back to
their defaults.

A file declaring a *newer* version than the build understands still loads;
you get a note in the log.

---

## Pitch

```ini
PitchMinimumDegrees=-45.0
PitchMaximumDegrees=85.0
```

The vertical look limits, in degrees. `0` is horizontal, positive looks
down from above, negative looks up from below.

| | Default | Accepted |
|---|---|---|
| `PitchMinimumDegrees` | `-45.0` | `-89` … `88` |
| `PitchMaximumDegrees` | `85.0` | `-88` … `89` |

The minimum must be strictly below the maximum, or both revert.

The hard stops at ±89° exist because the camera transform degenerates at
exactly ±90° — looking straight down, the horizontal heading becomes
undefined and the camera can spin.

**Tuning.** `85` is close to straight-down and is the interesting direction
for tactical positioning. Raising the minimum past about `-60` starts putting
the camera underground in most interiors, which is where
[floor protection](#collision) earns its keep.

---

## Sensitivity

Yaw (horizontal) and pitch (vertical) speed are set **per input device**, so
tuning one never moves another.

```ini
GamepadHorizontalSensitivity=2.0
GamepadVerticalSensitivity=1.0
TrackpadHorizontalSensitivity=1.0
TrackpadVerticalSensitivity=1.0
MouseHorizontalSensitivity=1.0
MouseVerticalSensitivity=1.0
```

| | Default | Accepted | Notes |
|---|---|---|---|
| `GamepadHorizontalSensitivity` | `2.0` | `0.10` … `4.00` | Gamepad yaw. `1.0` reproduces the game's own unmodified full-scale rate; `2.0` is the confirmed-good default. |
| `GamepadVerticalSensitivity` | `1.0` | `0.10` … `3.00` | Gamepad pitch. Multiplies the camera's own angular rate. |
| `TrackpadHorizontalSensitivity` | `1.0` | `0.10` … `8.00` | Trackpad two-finger yaw. Normalized multiplier over the proven internal base (`3.0`); `1.0` reproduces the current swipe exactly. |
| `TrackpadVerticalSensitivity` | `1.0` | `0.10` … `3.00` | Trackpad precise vertical swipe pitch. |
| `MouseHorizontalSensitivity` | `1.0` | `0.10` … `8.00` | Scales BG3's native middle-drag yaw (the `+0x14` field of the 109/110 events); `1.0` forwards them untouched. |
| `MouseVerticalSensitivity` | `1.0` | `0.10` … `3.00` | Vertical look from a middle-button drag up/down. |

### Legacy aliases

Older config files keep working. A canonical per-device key always wins over
its alias, whatever the line order.

| Legacy key | Aliases | Default | Accepted |
|---|---|---|---|
| `HorizontalSensitivity` | `GamepadHorizontalSensitivity` | `2.0` | `0.10` … `4.00` |
| `VerticalSensitivity` | `GamepadVerticalSensitivity`, and `TrackpadVerticalSensitivity` when that canonical key is absent | `1.0` | `0.10` … `3.00` |
| `PointerRotateSensitivity` | `TrackpadHorizontalSensitivity`, normalized: the old `3.0` maps to the new `1.0` | `3.0` | `0.10` … `8.00` |

### Advanced: how pointer rotation is delivered

The defaults are the tested path. Change these only to work around a
specific problem, and expect to re-test in-game.

| | Default | Accepted | Notes |
|---|---|---|---|
| `PointerRotationNative` | `true` | `true` / `false` | `true` hands a trackpad/mouse swipe to BG3's own pointer rotation channel — the one the controller uses. `false` restores the older behaviour of writing the camera heading directly, which the three settings below only apply to. |
| `PointerRotationHoldsHeading` | `true` | `true` / `false` | Hold the per-frame follow correction still for the duration of a swipe so it is not partly taken back in the same frame it lands. |
| `PointerRotateSmoothingMs` | `30` | `0` … `250` | How long unspent trackpad travel takes to become rotation. Native mode always keeps a 30 ms stability floor, so `0` selects that shortest stable response. |
| `PointerRotationFollowHoldSeconds` | `3600` | `0` … `3600` | How long after the last swipe the follow correction stays held. The default holds it for a whole swiping session so the camera stays where you put it; `0` holds it only while a swipe is in progress. A controller or keyboard rotation clears it at once regardless. |

> With no sensitivity keys in the file at all, behaviour is unchanged:
> gamepad `2.0` / `1.0`, trackpad `1.0` / `1.0`, mouse `1.0` / `1.0`. These are
> purely additive keys — no `ConfigVersion` bump.

<details>
<summary>Historical X-axis implementation notes (inactive)</summary>

`PointerRotateSmoothingMs` originally existed because accumulating travel fixed *how much*
a gesture turns the camera without fixing *how evenly*. A frame's step was
still however many samples happened to land inside it. Measured over a real
session, the gap between frames carrying trackpad travel has a median of 13 ms
but a 90th percentile of 63 ms and a 95th of 139 ms — so a swipe regularly goes
several frames with nothing at all, then arrives in a burst. The per-frame
rotation that produced had a median of 0.90° and a maximum of 11.10°: the
camera crawled, then jumped.

The follow correction used to hide this, being a rate-limited approach and so a
filter. Holding it still — see below — took the filtering with it, which is why
the unevenness only became visible once rotation started working at all.

Draining banked travel over a time constant separates arrival from spending:
travel is banked as the fingers move and spent as a continuous function of
frame time, so nothing depends on where a sample fell relative to a frame
boundary, and the silent frames keep moving. All of it still leaves the buffer,
so a gesture is worth the same rotation at any frame rate. Native mode always
keeps a 30 ms minimum because an unfiltered capture alternated between no
rotation and bursts as high as roughly 480°/s. Higher values soften it further.

`PointerRotationFollowHoldSeconds` exists because the follow correction is a
debt rather than a per-frame nuisance. Its error is `acos` of the angle between
where the camera looks and where the game wants it to look, with a one-degree
deadband, and the rate it is paid back at is `lerp(0, 90 deg/s, curve(error))`
— so it is proportional to how far a swipe has turned away, and it climbs the
further that is.

Holding it still only while a swipe is in progress therefore does not avoid it,
it defers it. The bill arrives at the next pause, as a lurch followed by a drag,
and it is larger the longer the swipe was. There is no target to move instead:
it is a direction derived from the game state, not a heading field, so the only
way not to be pulled back to it is not to be measured against it.

The default holds it for a session, so the camera stays where it was put. The
cost is that it no longer drifts to follow the character on its own while the
trackpad is aiming it. Rotating with a controller or a key releases it
immediately whatever this is set to, so those paths keep the game's behaviour
exactly.

`PointerRotationHoldsHeading` exists because the two axes were not competing
with the same things. The camera update runs a follow correction over the
heading — it turns the camera toward a target at an authored rate, and it runs
after the input is applied and before the picture for that frame is built. A
heading written from outside that pipeline is therefore partly taken back in
the same frame it lands, so a swipe drifts toward where it was pointed instead
of tracking the fingers.

Pitch never had the problem, and not because it is better behaved. Pitch is
delivered through the one function the game asks for it from, and this mod owns
that function; nothing downstream can drag a value the game has to request. The
setting builds the same guarantee for rotation by holding the correction still
while a swipe is in progress and releasing it the moment the swipe ends.

It is scoped to that window on purpose. Outside it the camera's own follow
behaviour is untouched, and a controller never went through this path at all.
Set it to `false` to get the previous behaviour back.

Horizontal rotation carries two multipliers because a stick and a trackpad do
not arrive in the same units. A stick reports its deflection: it spans the
whole `0..1` range and rests at zero. A trackpad reports how far the fingers
travelled since the last sample, which is a speed — runtime captures put a
swipe anywhere from `0.1` for a crawl to beyond `4.0` for a flick — and it
never rests anywhere, because it only reports while a finger moves.

In RPG mode a wheel's rotation is driven by finger travel. Every trackpad
sample says how far the fingers moved since the last one, so those amounts are
accumulated, and each frame drains a share of the accumulator into the
rotation the game is asked for. When the fingers stop, no travel arrives, the
accumulator empties, and rotation ends on its own — there is no timeout
deciding when a gesture is over, because the input already answers that.

Rotation is therefore proportional to how far your fingers travel, not to how
hard the mod guesses you are pushing. `PointerRotateSensitivity` sets how much
camera rotation one unit of travel buys.

Only a device that reports a magnitude other than `0` or exactly `1.0` is
treated this way, which is how a wheel is told apart from a key: a key can
only report a full `1.0`. Keyboard rotation, the right stick, and every other
camera mode keep the game's own per-event handling.

</details>

The default horizontal value is `2.0` rather than `1.0` because the stock
rate feels slow once you also have a vertical axis to move through.

The two axes are not directly comparable in the game's own terms: the
horizontal rate comes from a constant inside the game, the vertical from the
camera's `rotationSpeed` field. The defaults were matched by eye so both
axes feel like one stick.

---

## Zoom

```ini
ZoomSensitivity=0.6
ZoomResponseCurve=2.0
ZoomMinimumResponse=0.12
ZoomForceActive=true
StickDeadzone=0.15
```

Zoom is **hold L3 + right stick vertically**.

### The problem these solve

The game's camera action handler discards any stick magnitude at or below
`0.65` and rescales what is left over `(0.65, 1.0]`. That is a step, not a
ramp: zoom does nothing at all, then suddenly moves at the gate's own speed.

Worse, lowering sensitivity the obvious way made it *worse* — scaling the
payload down just pushed more of the stick's travel under the gate, widening
the dead band.

These four options invert that gate instead: normalise out of your own
deadzone, shape the result, then map it back through the gate so the ramp
starts exactly where your deadzone ends.

| Option | Default | Accepted | What it controls |
|---|---|---|---|
| `ZoomSensitivity` | `0.6` | `0.05` … `3.00` | Top speed at full deflection. Not the distance limits. |
| `ZoomResponseCurve` | `2.0` | `1.00` … `4.00` | `1.0` linear. Higher spends more travel on the slow end. |
| `ZoomMinimumResponse` | `0.12` | `0.00` … `0.50` | Speed the instant you leave the deadzone. |
| `ZoomForceActive` | `true` | bool | Force the axis "active" past the deadzone. |
| `StickDeadzone` | `0.15` | `0.00` … `0.80` | Shared by zoom and rotation. |

### `ZoomForceActive`

The input event carries an "axis is active" byte alongside its magnitude,
and the game consults **that byte**, not the magnitude, when deciding whether
the zoom action runs at all. Reshaping the magnitude alone never moved the
point where zoom began responding. Set to `false` to restore the game's own
behaviour.

### Trackpad and mouse: `PointerZoomModifierEvent`

By default, **two-finger scroll on a trackpad pitches the camera** — the mod
intercepts the game's `CameraZoomIn`/`CameraZoomOut` actions and turns them
into true pitch, exactly as it does for the controller's right stick.

To get *distance* zoom from a pointer device, you need a modifier: a button
that plays the same role L3 does on a controller. Hold it and the vertical
axis zooms; release it and it pitches again.

```ini
PointerZoomModifierEvent=0
```

`0` disables it. **No default can be shipped**, because the numeric id of a
mouse or trackpad button is assigned by the game's own input map, is not
documented anywhere, and differs from the controller ids this mod already
knows. Guessing one would mean writing to an input the mod has not verified.

#### Keyboard zoom, trackpad pitch: `ZoomDeviceId`

The simplest arrangement, and the one that needs nothing held.

Baldur's Gate 3 has **one** action for zoom, `CameraZoomIn` /
`CameraZoomOut`, and this mod turns it into pitch — which is why two-finger
scroll and the right stick both pitch the camera.

`ZoomDeviceId` names one input device whose version of that action keeps its
original meaning. Bind the same action to a key as well, name the keyboard
here, and you get:

- **Two fingers up/down** → pitch
- **Two fingers left/right** → horizontal rotation, unchanged
- **Page Up / Page Down** → zoom

No modifier, no chord, no holding.

```ini
ZoomDeviceId=-1
```

`-1` disables it. Note it is **not** `0`: device ids start at zero, and on
this build `0` is a real device, so using it as the off switch would make
that device unselectable.

**Setting it up:**

1. In game, **Options → Controls**, bind `Camera Zoom In` to `Page Up` and
   `Camera Zoom Out` to `Page Down`, keeping the existing bindings too.
2. Set `VerboseLogging=true`, start the game, press Page Up once.
3. In `mod.log` look for:

   ```
   [RUNTIME] camera action 104 arrived from device=0 magnitude=1.0000; ...
   [RUNTIME] camera action 104 arrived from device=1 magnitude=0.1000; ...
   ```

   One line per device that produces the action. The magnitude tells you
   which is which without guessing: a key is digital and arrives at full
   scale, while a scroll notch arrives at a small fixed value. Take the id
   from the line with the **larger** magnitude — that is the keyboard.
4. Put the keyboard's id in `ZoomDeviceId`, set `VerboseLogging=false`,
   restart.

**Tuning how coarse it feels:**

```ini
ZoomDeviceSensitivity=0.25
```

A key is digital — it is either down or not. There is no travel to ease
into, so the step size is the only thing that decides how abrupt zooming
feels, and it needs to be smaller than the value a stick uses. Lower it
toward `0.10` for finer control, raise it toward `0.50` to cover distance
faster.

> Pick the id that appears when you press the **key**, not the one that
> appears when you scroll — naming the trackpad here would make scrolling
> zoom and remove the pitch control this mod exists to provide.

#### The modifier option: `PointerZoomHoldButton`

```ini
PointerZoomHoldButton=2
```

**Hold a mouse button and scroll to zoom; release it and the same scroll
pitches again.** `0` off, `1` left, `2` right, `3` middle. On a trackpad,
`2` is a two-finger click held down.

This needs no discovery step and no event id. It reads the button's real
physical state from the window server, which is why it works where the
event-based modifier below does not: the game reports a mouse button as a
press immediately followed by a release *even while the button stays down*,
so an event-driven hold is active for microseconds and no scroll ever lands
inside it.

The controller has always worked this way too — L3 is polled from the
controller's own button field rather than reconstructed from events.

> Whatever the button normally does in game still happens, since the mod
> only observes it. Holding right-click will also do what right-click does.
> If that gets in the way, use `1` or `3` instead, or the event-based
> modifier below in toggle mode.

#### Hold or toggle

```ini
PointerZoomModifierToggle=false
```

`false` — hold the button; releasing it returns the axis to pitch. This
needs the game to report **both** a press and a release for that button.

`true` — each press flips between pitch and zoom. Use this for a button the
game reports only a press for; holding cannot work there, because nothing
would ever turn the zoom back off.

The discovery log below prints the phase of every id it sees, so which mode
a button needs is something you read rather than guess. An id that appears
twice, `phase=press` and `phase=release`, works with either mode. An id that
appears only as `phase=press` needs `PointerZoomModifierToggle=true`.

#### Finding your button's id

1. Set `VerboseLogging=true` in `config.ini`.
2. Start the game through the launcher.
3. Press the button you want to use — a trackpad click, a middle click,
   whatever you intend to hold.
4. Quit, and open `~/Documents/BG3 Camera Unlock/mod.log`.
5. Look for lines like:

   ```
   [RUNTIME] input event id=2 (0x2) phase=press; set
   PointerZoomModifierEvent to 2 to use it as the zoom modifier
   [RUNTIME] input event id=2 (0x2) phase=release; ...
   ```

   On the build this was written against, ids `1`, `2` and `4` reported both
   phases and behaved like the three mouse buttons, with `2` corresponding to
   a right click — which is what a two-finger click on a trackpad produces.
   Treat that as a starting point to verify, not a fact about your machine:
   ids come from the game's input map and are exactly what the log is for.

6. Put that number in `PointerZoomModifierEvent`, set `VerboseLogging` back
   to `false`, and restart.

Each id is reported only the first time it is seen, so the log stays short.
If several appear, press only your intended button and take the id that
shows up when you do.

> **A trackpad click may not produce an event at all** if the game has
> nothing bound to it. In that case, bind something to it first in
> **Options → Controls**, or pick a key you already have bound and hold that
> instead — the modifier does not have to be a mouse button.

### Tuning recipes

| Symptom | Change |
|---|---|
| Dead band near stick centre | Raise `ZoomMinimumResponse` to `0.20` |
| Too twitchy to pick a distance | Raise `ZoomResponseCurve` to `3.0` |
| Too slow overall | Raise `ZoomSensitivity` to `1.0` |
| Drifts when stick is at rest | Raise `StickDeadzone` to `0.20` |
| Want the vanilla feel back | `ZoomForceActive=false`, `ZoomResponseCurve=1.0` |

---

## Zoom distance

```ini
MinimumZoomDistance=0.5
MaximumZoomDistance=0
```

How close and how far the camera arm may go, in world units. **`0` means
"keep whatever the game itself authored."**

| | Default | Accepted |
|---|---|---|
| `MinimumZoomDistance` | `0.5` | `0`, or `0.05` … `50.00` |
| `MaximumZoomDistance` | `0` | `0`, or `1.00` … `500.00` |

`0.5` is close enough to fill the frame with a character's face. Going much
below about `0.3` puts the camera inside the character model.

The maximum defaults to `0` because the game's own pull-back limit is
already generous, and exceeding it makes BG3's level streaming visibly drop
distant geometry.

These are applied by widening the arm bounds the game stores in its camera
definitions, and the authored values are **restored when the mod unloads**.
The override is skipped entirely if those fields no longer read like an
ordered pair of plausible distances — a safety check against a game update
moving them.

---

## WASD movement

```ini
KeyboardMovement=true
```

**This key only does anything in the "Camera + WASD" flavor.**

In **Camera + WASD**: when enabled (the default there), the mod removes the
one keyboard-mode branch that prevents BG3's existing `CharacterMoveForward`,
`CharacterMoveBackward`, `CharacterMoveLeft` and `CharacterMoveRight` actions
from reaching the character-movement task. The launcher supplies the W, A, S
and D bindings. Controller mode already falls through that branch, so its
movement path is unchanged. Set this to `false` to leave the original
instruction untouched and play with camera control only.

In **Camera Only**: the key is still parsed so a config shared between the
two flavors does not error, but it is **forced to `false`** and cannot
enable WASD. That flavor never patches the guard and never binds W/A/S/D.
The mod log records the override if the file asked for `true`.

---

## Collision

```ini
ObstacleCollision=true
FloorProtection=true
CollisionSafetyMargin=0.20
FloorSafetyOffset=1.00
```

**Both are experimental.** The base game never points the camera at these
angles, so no single sweep handles every room.

| Option | Default | Accepted | What it does |
|---|---|---|---|
| `ObstacleCollision` | `true` | bool | Sweeps the camera arm against walls and props using the game's own raycast. |
| `FloorProtection` | `true` | bool | Queries the floor beneath the camera and shortens the arm to stay above it. |
| `CollisionSafetyMargin` | `0.20` | `0.00` … `2.00` | Clearance kept from a wall. |
| `FloorSafetyOffset` | `1.00` | `0.00` … `3.00` | Clearance kept from the floor. |

`FloorSafetyOffset` defaults to a full world unit, not something small,
because clearing the floor *mathematically* is not enough: the camera's near
clipping plane still crosses it and you see through the floor. One unit
reserves room for the camera volume itself.

### Tuning

| Symptom | Change |
|---|---|
| Camera dips through floors | Raise `FloorSafetyOffset` to `1.50` |
| Camera clips into walls | Raise `CollisionSafetyMargin` to `0.40` |
| Zoom jitters near geometry | Lower both, or disable the relevant layer |
| Camera yanked toward you for no reason | `FloorProtection=false` |
| Want it fully off | Set both booleans to `false` |

Turning these off does not disable the mod; you simply get the unlocked
camera with no collision assistance, which some players prefer in open
outdoor areas.

---

## Logging

```ini
VerboseLogging=false
```

Startup, configuration, and pattern verification are **always** logged —
those are the lines a bug report needs. This option adds per-event camera
state changes on top, which is noisy and only useful when investigating a
specific problem.

- Mod log: `~/Documents/BG3 Camera Unlock/mod.log`
- Launcher log: `~/Documents/BG3 Camera Unlock/launcher.log`

An `[ACTIVE]` line means every signature matched and every hook installed.
An `[ABORT]` line means a fail-closed check stopped the mod and the game is
running completely unmodified.

---

## Restoring defaults

Delete the file and relaunch:

```sh
rm ~/Documents/BG3\ Camera\ Unlock/config.ini
```

A fresh copy with documented defaults is written on the next launch.
