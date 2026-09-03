# Changelog

## Unreleased

- Head tracking now reaches the rendered frame. The pinned camera function was
  the view matrix history push, not the camera update, so the head pose lived
  only for the duration of that call and reached nothing but the frame's motion
  vectors - a smear across a view that never moved. The pose is now composed in
  before the view matrix is derived and left in place for the rest of the frame,
  with a second hook on the view manager's camera update taking it back out
  before the engine interpolates from it.
- Yaw and pitch corrected at the engine boundary, checked against a running
  game.
- Head tracking is now off outside a race. The gameplay gate compared two view
  manager fields that were never equal, so it read gameplay everywhere including
  the main menu. It now identifies the active camera controller by its vtable
  and requires a live race session on top of it - Wreckfest renders its main
  menu through the same in-game car camera a race does, so the camera alone
  cannot tell them apart.
- Head tracking now stops while the game is paused. The pause menu draws the
  same cockpit through the same in-race camera and the race session stays live
  across it, so neither of the gate's existing inputs could see it and the head
  kept moving the camera behind the menu. The gate now also reads the engine's
  own global pause byte, which the pause menu and a window that loses focus both
  raise through the engine's SetPaused. The view snaps to the game's camera when
  the menu opens and back to the tracked one when it closes; the pose pipeline
  keeps advancing throughout, so it resumes from where your head is then rather
  than from where it was when you paused. A build whose pause offsets are not in
  the profile, or whose chain cannot be read, keeps following through the pause
  menu and says so in the log rather than going dormant.
- Head tracking now follows in online races as well as local ones. The gate
  used to call the engine's bgMultiplayerGameGet() through the machine context
  and shut for anything it could not positively read as a local session, which
  cost every online player the mod. The mod composes the head pose into the
  camera transform for the frame being drawn and takes it back out before the
  engine interpolates from it, so car control, physics and everything sent over
  the wire read the camera the game computed - there is nothing for a server or
  another player to see. The whole session check is gone with it, and with it
  the mod's only indirect call into an engine function and the two pinned
  offsets (`machine_context_ptr_rva`, `machine_context_is_multiplayer`) that a
  patch could move. The gate is now the view state and the race session flag,
  both plain data reads.
