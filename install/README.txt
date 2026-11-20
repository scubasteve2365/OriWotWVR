Ori and the Will of the Wisps VR
VR mod for Ori and the Will of the Wisps (Steam).
Native stereo, played with a gamepad, as a diorama floating in front of you.
This mod is free. If you enjoy it, you can buy me a coffee: https://buymeacoffee.com/scubasteve2365

You need
- The game on Steam.
- A PC VR headset with an OpenXR runtime. Tested: SteamVR (set as your OpenXR runtime in SteamVR settings) and Virtual Desktop's VDXR on a Meta Quest 3.
- A gamepad (Xbox type).

Install
1. Unzip this folder anywhere.
2. Double click "Install OriWotWVR.bat" and type 1.
If another mod already uses winhttp.dll in the game folder (the randomizer does), the installer renames it to winhttp_chain.dll and this mod loads it first, so both keep working.

Play
1. Start SteamVR or connect Virtual Desktop.
2. Start the game from Steam. VR starts by itself.
3. Load a save. A stage floats in front of you.

Settings
Hold both sticks in for half a second to open the VR settings menu, or press Insert on the keyboard. The game pauses and gets no input while it is open. LB and RB (Page Up and Page Down) switch between the Stage, Picture and Menu tabs. Up and down choose a setting, left and right change it, A (Enter) selects, B (Esc) closes. Every row explains itself at the bottom of the menu.
Stage
- Stage distance and Stage size: where the stage floats and how big it is.
- Depth: how strong the 3D is. Higher makes the world feel smaller and deeper. If far backgrounds strain your eyes, lower it or raise Pop-out.
- Pop-out: brings more of the scene out toward you. It also eases far backgrounds.
- Screen shake: the game shakes the camera for bosses, thunder and big hits. In VR that shakes the whole stage, so it is Off by default.
- Recenter: puts the stage in front of you again. Your headset's own recenter does the same.
Picture
- Resolution: 100% matches your headset's pixel density across the stage. Takes effect the next time you start the game.
- Sharpen: crisper edges. Too high looks harsh. 0% turns it off.
- Picture: Auto picks Steady on Virtual Desktop and Floating Panel on everything else. Steady draws the stage into each eye's view and holds still under Virtual Desktop. Floating Panel can look a little sharper, but Virtual Desktop squashes and shears it when you turn your head.
- Surround: what you see around the stage. Black is the original look. Green fills it with pure green for Virtual Desktop's chroma-key passthrough, so your room shows around the game. Transparent asks your headset software to show your room itself, where it offers that, otherwise black.
Menu
- Sounds, Menu size, Menu height, Menu distance, Reset to defaults, Close.

The mod also sets these in the game on first start: HDR off, dynamic resolution off, motion blur off, resolution scale 100%, graphics quality Balanced. VSync and the game's frame limit stay off while VR runs, because the mod paces the game to your headset. You can change the others back in the game's options.

Uninstall
Double click "Install OriWotWVR.bat" and type 2. No game files are changed by this mod.

Performance tips
- SteamVR: in Settings, Video, Per-application video settings, set the game's Throttling Behavior to Fixed at your refresh rate, and Motion Smoothing off.
- 72 or 90 Hz is plenty for this game.

Good to know
- The mod keeps the game offline: it stops the game's online calls (PlayFab sign-in and telemetry). Spirit Trials leaderboards will not work while it is installed.
- Screen shake is off, but a few other screen effects still move the picture. Defeating a boss plays a screen distortion effect that can feel disorienting. Future releases will hunt down more of these effects and turn them off.
- Closing the game can end with a Windows error report about the game. The game does this on its own, without the mod too.
- The game's HUD sits on the stage.
- No motion controls. A gamepad is required. PlayStation pads work through Steam Input (Steam, the game's Properties, Controller, Enable Steam Input).

If something goes wrong
- The game stays flat: open the menu (both sticks or Insert). If VR did not start, it says why. Check your OpenXR runtime is set (SteamVR or Virtual Desktop). If no runtime is set and SteamVR is installed, the mod uses SteamVR. Then look in OriWotWVR.log in the game folder.
- Windows warns about the bat file: the mod is not signed, so Windows may ask you to confirm. The bat is plain text; read it first if you like.
- Smart App Control: if it is on, Windows can block unsigned mod DLLs and the game runs flat. There is no workaround in the mod.
- When SteamVR starts, its dashboard may appear in front of the stage. Close it as usual.
- Settings are saved in OriWotWVR\OriWotWVR.ini in the game folder. Please attach OriWotWVR.log when you report a problem.

Tested on
SteamVR on the author's PC, and Meta Quest 3 through Virtual Desktop's VDXR. NVIDIA graphics. Other headsets should work through OpenXR but have not been tried. AMD and Intel graphics have not been tried.

Support: https://buymeacoffee.com/scubasteve2365

Version 1.0. Credits and licences are in THIRD-PARTY-NOTICES.txt. MIT licensed, by scubasteve2365.
This is a fan mod, not made or approved by Moon Studios or Xbox Game Studios.
