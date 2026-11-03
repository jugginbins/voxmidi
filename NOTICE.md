# VoxMidi - licence and credits

VoxMidi is free software: you can redistribute it and/or modify it under the terms of the
**GNU Affero General Public License, version 3** (AGPL-3.0-only). It is distributed in the hope that
it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE. See <https://www.gnu.org/licenses/agpl-3.0.html>.

Every source file carries the identifier `SPDX-License-Identifier: AGPL-3.0-only`.
The full licence text is in the `LICENSE` file.

## What this means for sharing

* You may use, copy, share and modify VoxMidi, for free, including commercially.
* If you give someone a **binary** (a build of the plugin), you must also give them, or point them to,
  the **source** for exactly that build, under the same licence. Publishing the GitHub repository and
  attaching the builds to its Releases does this.
* If you change VoxMidi and distribute it, your changes must be AGPL-3.0 too.
* You may not remove the licence notices.

## Third-party components

* **JUCE** (https://juce.com) - dual licensed AGPLv3 / commercial. VoxMidi uses it under the AGPLv3,
  which is why VoxMidi itself is AGPLv3. JUCE bundles its own dependencies (for example the VST3 SDK
  and the AudioUnit SDK) under their respective licences, listed in JUCE's `LICENSE.md`.
* **VST** is a trademark of Steinberg Media Technologies GmbH.
* Algorithms: McLeod Pitch Method (McLeod & Wyvill, 2005), linear predictive coding (Levinson-Durbin),
  Krumhansl-Schmuckler key finding. These are published methods; the code here is original.

## Independence

VoxMidi is an independent project. It contains no code from, and is not affiliated with or endorsed by,
Vochlea or its Dubler products. Please do not describe it as such.
