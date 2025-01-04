# OpenXcom [![Workflow Status][workflow-badge]][actions-url]

[workflow-badge]: https://github.com/OpenXcom/OpenXcom/workflows/ci/badge.svg
[actions-url]: https://github.com/OpenXcom/OpenXcom/actions

OpenXcom is an open-source clone of the popular "UFO: Enemy Unknown" ("X-COM:
UFO Defense" in the USA release) and "X-COM: Terror From the Deep" videogames
by Microprose, licensed under the GPL and written in C++ / SDL.

See more info at the [website](https://openxcom.org)
and the [wiki](https://www.ufopaedia.org/index.php/OpenXcom).

Uses modified code from SDL\_gfx (LGPL) with permission from author.

## Realistic accuracy and cover system
[Video](https://www.youtube.com/watch?v=S9lrOClZVQQ)

### Features:
* **Precise probabilities distrubution**. Accuracy number represents real chance to hit, which wasn't the case with "classic" x-com accuracy. New accuracy works just like dice rolling in D&D - for 40% accuracy shot, the game rolls random number in 1-100 range, where 1-40 counts as hit, and 41-100 as miss.
* **Less shooting spread**. Cone of fire is more tight, making it possible to use it tactically. You can intentionally aim to a target, trying to get other targets in fire range - to a greater efficiency than in "classic". Bear in mind, that units aim to the middle of the exposed part of target, so shots spread around that point.
* **Type of shot (aimed/snap/auto) affects spread**. For the same final accuracy, aimed shot will miss closer to a target, snap/auto will be less accurate, and auto with one-handed weapon will be even worse. Combined with reduced spread, it provides a variety of options, for example with high exposive type of ammo, which is much more potent now, especially at long ranges.
* **Soldiers raise weapon to aim**. For an aimed shot or while kneeling, soldiers will raise their weapons to eye level. That means, if they see an enemy - they can shoot it. For example, now it's possible to fire a shot targeting sectoid (16 voxels high unit) while kneeling behind a stone wall.
* **Cover matters!** For every shot, accuracy is multiplied by percent of targets surface, visible to attacker. taking into consideration how accuracy rolls, every bit of cover can determine the difference between life and death. A piece of wooden fence, landing gear of Skyranger or its ramp, high crops or even that lone head of cabbage. Everything.
* **Accidental suicide / frienly fire protection**. After rolling a miss, game looks for some "missing" trajectory. If target is far enough, that trajectory will never be selected if it ends on nearest two tiles from the attacker. That means, you can shoot with rockets or high explosive ammunition through windows, from Skyranger ramp, throught narrow gaps, near your nearest teammates without a fear of accidently blowing yourself up. *Not applies to guided-type weapons, like blaster launcher - they still use classic accuracy*
* **Less confusing close-range accuracy numbers**. In "classic" x-com, dispayed  accuracy doesn't match with real, especially in close range shooting, where the real one is much higher. In this mod, there are two formulas for close range. First one gives accuracy multiplier. For aimed shots, last 10 tiles to target give you additional +0.1 to accuracy multiplier, up to x2 for point-blank shot. For snap/auto, it's +0.2 multiplier for last 5 tiles. If your accuracy is not enough to get upper cap with x2 multiplier, another formula applies, where delta between that cap and your accuracy is divided by number of tiles to target evenly. You'll gain maximum possible accuracy for point-blank either way, but first formula makes it possible to get that number even from a distance, if soldier is good at shooting.
* **Min/max accuracy caps at 5%/95% respectively**. For 5% or less - kneeling grants additional 2%, and aiming another 3%

## 'Rejected features' fork

This is a fork from official master of OXCE+ which allows some new features which
have been rejected by official OXCE+ developers. Currently there are 2 new features
implemented:

### More than 8 bases

Now, player can choose the limit of available bases to build using a new entry in
file 'openxcom.cfg': 

oxceMaxBases= maxNumBases

Default is 8 bases.
If more than the 8 visible bases, player can scroll bases miniview using RMB at the 
edges of the miniview. A blue arrow appears when there are more bases at either edge.
 
### Hangars reworked 
Every hangar and craft has a "hangarType" tag, so a Hangar with an specific "hangarType"
value can only store crafts with the same "hangarType". "hangaType" tag can be given 
at both facilities and crafts, in their corresponding YAML ruleset. If no tag is given, 
that craft/hangar will have a "-1" default tag, so their behavior will be the same
as in vanilla engine.
Another feature added is the possibility of allocate more than 1 craft at an specific
hangar facility, so all crafts will be shown at Basescape, and can be right-clicked
to go to craft screen. If more than craft is defined for an hangar facility, a new 
YAML tag "craftSlots" should be defined in the hangar ruleset. This tag will be 
followed by as many "position entries" as crafts are allowed in the hangar.
Every position is a "- [x, y, z]" entry, where x,y are position offesets respect to 
the center of the hangar sprite, and z is ignored. This way, modders can choose where
they want to place crafts in an hangar which allows more than one.  

An "example MOD" is included in "Examples" folder. Though this mod is a submod of the
fantastic master mod "The Xcom Files", you can adapt it to any other mod or vanilla 
game, which needed using differentiated hangars and/or more than 1 craft per hangar.

### Windows Binary

For convenience, a ZIP file with windows binary has been added to folder 'WindowsBinaries'.
 This exe has been tested to work in a Windows10 machine (64 bits), thought it has not been
intensively tested. It also include extra Language files needed for some specific features

[Link to Windows Binary](/WindowsBinaries/UnzipInOXCEFolder.zip)

You can just place the binary and folder in the game folder of the original one, and just click
it when you want start the game with these new features.

## Installation

OpenXcom requires a vanilla copy of the X-COM resources -- from either or both
of the original games.  If you own the games on Steam, the Windows installer
will automatically detect it and copy the resources over for you.

If you want to copy things over manually, you can find the Steam game folders
at:

    UFO: "Steam\SteamApps\common\XCom UFO Defense\XCOM"
    TFTD: "Steam\SteamApps\common\X-COM Terror from the Deep\TFD"

Do not use modded versions (e.g. with XcomUtil) as they may cause bugs and
crashes.  Copy the UFO subfolders to the UFO subdirectory in OpenXcom's data
or user folder and/or the TFTD subfolders to the TFTD subdirectory in OpenXcom's
data or user folder (see below for folder locations).

## Mods

Mods are an important and exciting part of the game.  OpenXcom comes with a set
of standard mods based on traditional XcomUtil and UFOExtender functionality.
There is also a [mod portal website](https://openxcom.mod.io/) with a thriving
mod community with hundreds of innovative mods to choose from.

To install a mod, go to the mods subdirectory in your user directory (see below
for folder locations).  Extract the mod into a new subdirectory.  WinZip has an
"Extract to" option that creates a directory whose name is based on the archive
name.  It doesn't really matter what the directory name is as long as it is
unique.  Some mods are packed with extra directories at the top, so you may
need to move files around inside the new mod directory to get things straighted
out.  For example, if you extract a mod to mods/LulzMod and you see something
like:

    mods/LulzMod/data/TERRAIN/
    mods/LulzMod/data/Rulesets/

and so on, just move everything up a level so it looks like:

    mods/LulzMod/TERRAIN/
    mods/LulzMod/Rulesets/

and you're good to go!  Enable your new mod on the Options -> Mods page in-game.

## Directory Locations

OpenXcom has three directory locations that it searches for user and game files:

<table>
  <tr>
    <th>Folder Type</th>
    <th>Folder Contents</th>
  </tr>
  <tr>
    <td>user</td>
    <td>mods, savegames, screenshots</td>
  </tr>
  <tr>
    <td>config</td>
    <td>game configuration</td>
  </tr>
  <tr>
    <td>data</td>
    <td>UFO and TFTD data files, standard mods, common resources</td>
  </tr>
</table>

Each of these default to different paths on different operating systems (shown
below).  For the user and config directories, OpenXcom will search a list of
directories and use the first one that already exists.  If none exist, it will
create a directory and use that.  When searching for files in the data
directory, OpenXcom will search through all of the named directories, so some
files can be installed in one directory and others in another.  This gives
you some flexibility in case you can't copy UFO or TFTD resource files to some
system locations.  You can also specify your own path for each of these by
passing a commandline argument when running OpenXcom.  For example:

    openxcom -data "$HOME/bin/OpenXcom/usr/share/openxcom"

or, if you have a fully self-contained installation:

    openxcom -data "$HOME/games/openxcom/data" -user "$HOME/games/openxcom/user" -config "$HOME/games/openxcom/config"

### Windows

User and Config folder:
- C:\Documents and Settings\\\<user\>\My Documents\OpenXcom (Windows 2000/XP)
- C:\Users\\\<user\>\Documents\OpenXcom (Windows Vista/7)
- \<game directory\>\user
- .\user

Data folders:
- C:\Documents and Settings\\\<user\>\My Documents\OpenXcom\data (Windows 2000/XP)
- DATADIR build flag
- C:\Users\\\<user\>\Documents\OpenXcom\data (Windows Vista/7/8)
- \<game directory\>
- . (the current directory)

### Mac OS X

User and Config folder:
- $XDG\_DATA\_HOME/openxcom (if $XDG\_DATA\_HOME is defined)
- $HOME/Library/Application Support/OpenXcom
- $HOME/.openxcom
- ./user

Data folders:
- $XDG\_DATA\_HOME/openxcom (if $XDG\_DATA\_HOME is defined)
- $HOME/Library/Application Support/OpenXcom (if $XDG\_DATA\_HOME is not defined)
- DATADIR build flag
- $XDG\_DATA\_DIRS/openxcom (for each directory in $XDG\_DATA\_DIRS if $XDG\_DATA\_DIRS is defined)
- /Users/Shared/OpenXcom (if $XDG\_DATA\_DIRS is not defined or is empty)
- . (the current directory)

### Linux

User folder:
- $XDG\_DATA\_HOME/openxcom (if $XDG\_DATA\_HOME is defined)
- $HOME/.local/share/openxcom (if $XDG\_DATA\_HOME is not defined)
- $HOME/.openxcom
- ./user

Config folder:
- $XDG\_CONFIG\_HOME/openxcom (if $XDG\_CONFIG\_HOME is defined)
- $HOME/.config/openxcom (if $XDG\_CONFIG\_HOME is not defined)

Data folders:
- $XDG\_DATA\_HOME/openxcom (if $XDG\_DATA\_HOME is defined)
- $HOME/.local/share/openxcom (if $XDG\_DATA\_HOME is not defined)
- DATADIR build flag
- $XDG\_DATA\_DIRS/openxcom (for each directory in $XDG\_DATA\_DIRS if $XDG\_DATA\_DIRS is defined)
- /usr/local/share/openxcom (if $XDG\_DATA\_DIRS is not defined or is empty)
- /usr/share/openxcom (if $XDG\_DATA\_DIRS is not defined or is empty)
- the directory data files were installed to
- . (the current directory)

## Configuration

OpenXcom has a variety of game settings and extras that can be customized, both
in-game and out-game. These options are global and affect any old or new
savegame.

For more details please check the [wiki](https://ufopaedia.org/index.php/Options_(OpenXcom)).

## Development

OpenXcom requires the following developer libraries:

- [SDL](https://www.libsdl.org) (libsdl1.2)
- [SDL\_mixer](https://www.libsdl.org/projects/SDL_mixer/) (libsdl-mixer1.2)
- [SDL\_gfx](https://www.ferzkopp.net/wordpress/2016/01/02/sdl_gfx-sdl2_gfx/) (libsdl-gfx1.2), version 2.0.22 or later
- [SDL\_image](https://www.libsdl.org/projects/SDL_image/) (libsdl-image1.2)

The source code includes files for the following build tools:

- Microsoft Visual C++ 2010 or newer
- Xcode
- Make (see Makefile.simple)
- CMake

It's also been tested on a variety of other tools on Windows/Mac/Linux. More
detailed compiling instructions are available at the
[wiki](https://ufopaedia.org/index.php/Compiling_(OpenXcom)), along with
pre-compiled dependency packages.
