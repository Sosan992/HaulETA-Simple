# HaulETA Simple changelog

## 0.1.9

- With the TM Real Walk plugin: every panel (main panel, detached G-meter and route card) hides automatically when the driver gets out of the truck and comes back when he sits in the truck again. HaulETA reads TM Real Walk's `[walker] enter/exit at world` lines from the game's log (game.log.txt) four times a second. Without TM Real Walk nothing changes. In moving mode (Ctrl+F11) the panels stay visible.

## 0.1.8

- Versions are numbered 0.x.y from now on (0.1.8 follows 1.7; the window titles show "HaulETA Simple 0.1.8").
- Settings window (Ctrl+F12): the checkbox labels ("G-meter as a separate element", "Route separate") are now white and readable. Windows' visual styles drew them in a dark colour on the dark background; the checkboxes now draw without the visual style, so the plugin's white text colour applies.

## 1.7

- English is now the default language: a new install (no language saved in settings.ini yet) starts in English, and English is the first entry of the language drop-down. A language chosen earlier in Ctrl+F12 is kept.
- An unknown language code in settings.ini also falls back to English.

## 1.6

- Languages: a new "Nyelv / Language" drop-down in the Ctrl+F12 settings window. Besides Hungarian, the plugin is translated into the five languages most used by ETS2 players according to Steam review languages: English, Russian (Русский), Turkish (Türkçe), German (Deutsch) and Polish (Polski).
- Everything is translated: panel headings and texts, the route card, the "rest now" warning, the hotkey warnings, and the settings window itself (title, labels, theme names, checkboxes). The language is switched immediately and saved in settings.ini ([appearance] language = hu / en / ru / tr / de / pl); the default stays Hungarian.
- Texts that would not fit their column (e.g. the Turkish and Polish "rest now" warning) are drawn with a smaller font, and headings are shortened with an ellipsis instead of overflowing.

## 1.5

- Background themes: a new "Háttér" drop-down in the Ctrl+F12 settings window switches every panel between "Sötét (karbon)" (the existing dark look) and "Világos", a light theme based on a modern app design.
- Light theme: white paper card with a soft drop shadow, near-black text and grey headings, violet accent (arrival time, progress bar, fuel level), violet frames with light faces around the analog fuel gauge and the G-meter, and the local time in a black pill. The headlight and blinker edge glow work in both themes.
- New free CC0 texture for the light theme: ambientCG Paper001, scaled to 788 × 104 px (its aspect ratio is kept while tiling) and built into the DLL.
- The theme applies to the detached G-meter and route cards too and is saved in settings.ini ([appearance] theme = 0 dark, 1 light). The settings window itself stays dark.

## 1.4

- Ctrl+F12 is now a settings window: besides the transparency slider (now for every panel) it has checkboxes for detachable elements.
- "G-méter külön elemként": the G-meter leaves the panel and becomes its own small card (114 × 104 px) that can be placed anywhere on the monitor.
- "Útvonal külön": the route column leaves the panel and becomes a route card (400 × 104 px) showing where from and where to (city and company from the current job), the time left in large digits, the distance left, the estimated arrival and a progress bar. Without a job it shows "Nincs aktív fuvar" and still follows the GPS route.
- The main panel gets shorter by the detached columns (788 → 672 / 492 / 376 px).
- Detached cards appear above the main panel by default; move them with Ctrl+F11 and drag. Their positions and the checkbox choices are saved in settings.ini ([gmeter], [routecard], [layout]).
- The cards have the same look as the panel, including the back-lit headlight and blinker edge glow, and every window is kept inside the game window.
- Uses the job configuration of the telemetry SDK (source.city, source.company, destination.city, destination.company).

## 1.3

- G-meter for the Coaches DLC: a new column at the right end with a round brushed-metal dial. A dot shows the force the passengers feel (sideways and forward/back, 0.5 g at the edge) with a short fading trail; next to it the current value and a peak that is held for 5 s and then sinks slowly.
- Comfort colours for passengers: green under 0.15 g, amber under 0.30 g, red above.
- Uses the truck.local.acceleration.linear telemetry channel, smoothed and updated about 20 times a second; the rest of the panel is reused between updates, so it stays light on the CPU.
- The panel is now 788 × 104 px (was 672 × 104) and the window 816 × 132 px. The textures were re-sized from the same CC0 sources: carbon background 788 × 104, glow diffuser 816 × 132 and a new 62 × 62 brushed-metal bezel for the G-meter.
- The panel is always kept inside the game window: a position saved near the right edge for the narrower panel is shifted left on screen instead of being cut off.
- The panel position is now saved only when you move it (Ctrl+F11), so a temporary on-screen shift never overwrites the saved position.

## 1.2

- Headlight frame glow in the same back-lit style as the blinker glow: parking lights make the whole panel edge glow faint white, low beam a stronger white and high beam blue; with no lights on there is no glow.
- Follows the truck.light.parking, truck.light.beam.low and truck.light.beam.high telemetry channels. Switching between the stages cross-fades smoothly (white to blue and back).
- While a blinker flashes, its amber glow takes over on its side and the headlight colour returns in between the flashes.
- Uses the same CC0 frosted-plastic diffuser texture (ambientCG Plastic004, 700 × 132 px) as the blinker glow; no new texture files were needed.

## 1.1

- Blinker back-light: while the left or right indicator flashes, that side of the panel edge glows amber in step with the lamp, as if lit from behind (both sides for the hazard lights). The glow rises quickly and fades softly like a bulb.
- Uses the truck.light.lblinker / truck.light.rblinker telemetry channels; a free CC0 frosted-plastic texture (ambientCG Plastic004) diffuses the light, scaled to 700 × 132 px and built into the DLL.
- The overlay window is now 14 px larger on every side for the glow; the saved panel position is unchanged.
- Blinker animation frames reuse the already drawn panel, so the effect stays light on the CPU.

## 1.0

- First stable release: version number changed from 0.7 to 1.0. Same features and look as 0.7.

## 0.7

- New dark, modern look: carbon-fibre panel background with smooth anti-aliased rounded corners, shown with per-pixel transparency.
- Brushed-metal bezels around the fuel gauge and the clock; free CC0 textures from ambientCG, scaled to their exact on-screen sizes (672 × 104, 76 × 64 and 120 × 42 px) and built into the DLL.
- Bahnschrift typeface (Segoe UI fallback), spaced column headings and a blue accent colour; the estimated arrival time is highlighted.
- The fuel gauge stays analog: anti-aliased needle with a metal pivot, and the scale arc fills up to the current level (blue, amber under a quarter, red in the reserve zone).
- Route progress bar under the departure and arrival times.
- Fatigue figure redrawn smoothly; its fill is green, amber or red depending on the time left until the next rest.
- The clock shows large blue (day) / green (evening) digits on a dark glass face instead of seven-segment digits.
- Blue rim while the panel is in moving mode (Ctrl+F11); the transparency window uses the new colours.

## 0.6

- Fatigue figure drains from the top down: the head empties first, then the torso and arms, finally the legs down to the feet.

## 0.5

- Fatigue figure: the arms and legs now drain too, not just the torso. Every part empties at the same rate — head and torso from the top down, arms and legs from the hands and feet towards the torso.
- Live countdown under the figure showing the in-game time left until the next rest (h:mm); red under one hour, "PIHENJ!" when a rest is due, "--:--" without data.

## 0.4

- Flicker-free drawing: the panel is painted off-screen and copied in one step.
- Larger, more readable text throughout the panel and the transparency window; the panel is now 672 × 104 px.
- Fuel gauge redesigned: framed dial with E, 1/2 and F markings, major/minor ticks, a red reserve zone and a thicker needle.
- Larger seven-segment clock digits.
- American Truck Simulator (amtrucks.exe) is recognised as well as ETS2.
- The fatigue scale adapts to the largest rest value the game reports and can be set with [fatigue] full_minutes in settings.ini.
- If Ctrl+F11 or Ctrl+F12 cannot be registered, the panel shows it and a warning goes to the game log.
- Removed leftover code for the old separate clock and speed-limit windows; fonts are created once instead of on every repaint.

## 0.3

- Added a live analog fuel gauge using current fuel amount and the truck's tank capacity.
- Added an outlined human figure whose black fill decreases as the next rest state approaches.
- Reworked the center layout so both indicators fit between the arrival time and local clock.

## 0.2

- Increased the combined panel size to 550 × 90 px.
- Removed the clock bitmap skin; the functional digital clock is drawn directly into the panel.
- Smoothed the speed-limit circle by drawing it at four times the target resolution and downsampling it.
- Kept Ctrl+F11 panel movement and Ctrl+F12 transparency controls.

## 0.1

- First Simple release: one 550 × 85 px panel combines route departure/ETA, the red GPS speed-limit circle, and the local-time clock.
- Reuses the 0.8 visual style and clock skin; clock digits change from blue to green at in-game 17:30.
- Ctrl+F11 moves the combined panel; Ctrl+F12 opens its saved transparency slider.
