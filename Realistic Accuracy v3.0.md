# REALISTIC ACCURACY
## version 3.0

Realistic Accuracy & Cover system is the (optional) overhauled shooting / accuracy mechanics, developed and added to Brutal OXCE by Joy Narical. In a nutshell - units can use other units / objects / terrain as a reliable  cover, shots have lesser angular dispersion, and it's safe to shoot near obstacles, including allies.

Please leave feedback & bugreports:
[Github](https://github.com/narical/openxcom-accuracy/issues) / [Forum post](https://openxcom.org/forum/index.php?topic=11928.0)

[Youtube video](https://www.youtube.com/watch?v=eYL4JHkORzA)

**Features:**
* **Display chance to hit on cursor.** Instead of fixed accuracy number, RA uses calculated chance to hit, based on distance and shot accuracy. Chances were collected by simulation - for every pair of accuracy value (in range 0..120, step 2) and distance (in range 1..40) 20.000 simulations were performed, with results postprocessed with filter to reduce statistical artefacts. *That's almost 50 millions simulated shots in total!*
* **Classic accuracy - Display chance to hit on cursor** You can enable this option for Vanilla accuracy (When RA is off). If you perform a shot from 2 tiles distance with accuracy 20, chance to hit will be ~90% - enabling this option will change displayed 20% number at cursor to 90%, nothing more.
* **Classic accuracy - Reaction fire threshold uses chance to hit.** Available as an option. Even with low accuracy and high reaction stat, a soldier won't waste TUs until an enemy comes close enough for a decent shot. Reminder: "reaction fire threshold" is configurable menu option, placed in OXCE settings block.
* **Reversed algorithm.** Realistic Accuracy uses calculated chance to hit as a base number, and works just like dice rolling in D&D - for 40% accuracy shot, the game rolls random number in 1-100 range, where 1-40 counts as hit, and 41-100 as miss. This grants numerous possibilities.
* **Less shooting spread.** Cone of fire is more tight, making it possible to use it tactically. You can intentionally aim to a target, trying to get other targets in fire range - to a greater efficiency than in "classic". Bear in mind, that units aim to the middle of the exposed part of target, so shots spread around that point.
* **Type of shot (aimed/snap/auto) affects spread.** For the same final accuracy, aimed shot will miss closer to a target, snap/auto will be less accurate, and auto with one-handed weapon will be even worse. Combined with reduced spread, it provides a variety of options, for example with high exposive type of ammo, which is much more potent now, especially at long ranges.
* **Soldiers raise weapon to aim.** For an aimed shot or while kneeling, soldiers will raise their weapons to eye level. That means, if they see an enemy - they can shoot it, without "No line of fire" error. *Player can enable this option for snap/auto fire modes as well ("RA - Improved snap/auto" option)*
* **Cover mechanics.** Chance to hit is affected by percent of targets surface, visible to attacker. Good cover reduces chances for a unit to be hit - but increases chances for incoming shot to hit and destroy that cover.
* **Off-center shooting works in a different way.** If enabled, it allows units to check chance to hit (not just LoF as before!) from shifted positions, and select the best one - which allows shooting from behind a cover with a much better effectiveness. X-Com operative can hide behind Skyranger's wheel and aim from around the corner without penalty to enemy visibility.
* **Accidental suicide / frienly fire protection.** After rolling a miss, game looks for some "missing" trajectory. If target is far enough, that trajectory will never be selected if it ends on nearest two tiles from the attacker. That means, you can shoot with rockets or high explosive ammunition through windows, from Skyranger ramp, throught narrow gaps, near your nearest teammates without a fear of accidently blowing yourself up. Not applies to guided-type weapons, like blaster launcher - they still use classic accuracy. *Beware of force-fire with CTRL - it overrides suicide protection!*
* **Cover efficiency.** How much cover affects chances to hit a target:
    "no effect" 0% - target exposure doesn't affect final accuracy
    "medium effect" - 50% fraction of accuracy is multiplied by visibility
    "high effect" - 70% fraction of accuracy is multiplied by visibility
    "full effect" - 100% final accuracy is multiplied by visibility
* **Sniping bonus mitigates target's cover.** Bonus is equal to final accuracy above 100%, divided by 2.

**Planned / possible features:**
* **Unlucky RNG protection.** New menu option. If on, the game prevents several unlucky RNG rolls in a row, by forced use of mathematical expectation value.
* **Enable RA for Arcing shots and shotguns.** Now it's disabled, those type of weapons don't use any RA features, including suicide protection etc.

**Additional details / clarifications:**
* All RA additional options work only when the main RA option is on.
* When RA option is off - the game should works the same as OXCE... but it doesn't. To make RA possible, I've done many changes to the code, which affects "vanilla" accuracy, including fixes and improvements, but also new bugs.
* Forced shot with CTRL (with RA on) has additional feature and works slightly different - shows the percent of target's exposure (in other words, how much cover it has). When displaying % exposition, it has two little arrows from sides. Now, the differences. If target is partially visible, pressing CTRL will give exposition number. If target doesn't have line of sight - it will show "classic" Extender accuracy number in gold color.
* For RA, LoS/LoF origin point calculation is changed, big units (like hovertank and cyberdisk) are more cumbersome in terms of finding LoF, they don't have off-center aiming ability, and can't shoot "from around the corner" as small live creatures, which gives some advantage against them to small units.
* There's First-Person View screenshot function. It makes a picture from unit's eyes perspective. At the moment, it doesn't take into account off-center shooting option, so if you get blocked view in picture but unit still sees and shoots enemy in game - it's ok. You just can't get two additional pictures with shifted line of sight
* Initially, this mod was intended to use with classic UFO1/2 games, without megamods/total conversions in mind. But as BrutalAI is widely used to play mods, I was forced to make RA work with them to - with mixed results. There are plenty of new mechanics, extensive use of weapons which had never been used in vanilla games, extensive use of arcing-type weapons... But the most obvious issue is game balance. Sometimes it's weird behind any sense, and I honestly cannot balance RA to please everyone. I still do my best, but balancing RA around XCF to make all those 100500 weapons good and realistic is far beyond my goals.