<img width="1200" height="350" alt="s2x-readme-banner" src="https://github.com/user-attachments/assets/733e21a4-2703-4ba7-ab07-151b0f17e94a" />

# S2x ⭐

S2x is a custom client project for **Call of Duty®: WWII**, focused on preserving and extending functionality for campaign, multiplayer, and zombies.

The project is inspired by the work of the former XLabs community, but S2x is an independent project and is not affiliated with XLabs, Activision, Sledgehammer Games, Microsoft, or any related publisher, developer, or trademark holder.

## Requirements

You must own a legitimate Steam copy of **Call of Duty®: WWII** to use S2x.

S2x does **not** provide game files, cracked executables, or any method to obtain the game without purchasing it. Please support the original developers and publishers by owning a legal copy of the game.

## Roadmap

- [x] Basic Steam API emulation
- [x] Basic Demonware emulation
- [x] Offline multiplayer/zombies/campaign support
- [x] Disable anti-debug checks
- [x] Disable integrity checks
- [x] Disable code healing
- [x] Restore internal/external console
- [ ] Dedicated servers
- [ ] Basic modding support

## Compile from source code

- Clone the Git repository using [Git](https://git-scm.com/install/windows) or [GitHub Desktop](https://desktop.github.com/download/). **Do not download it as a ZIP**, as that will not include the required submodules.
- Run the `generate.bat` script to generate the project solution.
- Build the project using the generated solution file at `build\s2x.sln`.

## Credits

- [momo5502](https://github.com/momo5502) - Former lead developer of [XLabsProject](https://github.com/XLabsProject), research, codebase, and Sogen.
- [Auroramod](https://github.com/auroramod) - Multiple components, UD2 patching, and Demonware emulation.
- [Mallgrab](https://github.com/mallgrab/CWHook) - Cold War Arxan research.
- [Wanted](https://github.com/WantedDV) - `find_lea_target` helper and feedback on VirtualAlloc hook research.

## Disclaimer

S2x is an independent community project created for educational, research, preservation, and interoperability purposes.

This project is not affiliated with, endorsed by, sponsored by, or approved by Activision Publishing, Inc., Sledgehammer Games, Microsoft, Steam, Valve, XLabs, or any related companies.

Call of Duty, Call of Duty: WWII, and all related names, logos, assets, and trademarks are property of their respective owners.

S2x does not include or distribute copyrighted game files. Users are required to own a legitimate copy of the game.

The maintainers are not responsible for misuse of this software. Use responsibly.
