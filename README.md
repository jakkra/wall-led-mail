# Wall Led-strip
Very simple ambient light thingy that that also fetches unread emails from my [server](https://github.com/jakkra/OneBackendToRuleThemAll) and sets 1 LED red for each unread email.

There is a basic API for controlling it. It's a very limited subset of what [WLED](https://github.com/Aircoookie/WLED) supports, for easy integration with [existing systems](https://github.com/jakkra/HomeController).

## Images
<img src="/.github/comparison.jpg"/>
<img src="/.github/email_demo.gif" width="840"/>

## CAD
Full Fusion 360 project is found in `CAD` folder.

Pretty much a bunch of ~20cm 3D printed pieces glued together.

## Compiling
Follow instruction on [https://github.com/espressif/esp-idf](https://github.com/espressif/esp-idf) to set up the esp-idf, then just run `idf.py build` or use the [esp-idf VSCode extension](https://github.com/espressif/vscode-esp-idf-extension). Used esp-idf version: is 4.1.