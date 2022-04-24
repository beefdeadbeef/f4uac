F4UAC -- USB Audio Class headphones on STM32F4

- Wait, what ? Again ? *sigh* yeah, again.

## Abstract
Loose implementation of excellent ST appnote [AN5142](https://www.st.com/resource/en/application_note/dm00477514-classd-audio-amplifier-implementation-on-stm32-32bit-arm-cortex-mcus-stmicroelectronics.pdf),
using so-called `Black Pill` [boards](https://github.com/WeActTC/MiniSTM32F4x1).

## Requiremens
-  arm-none-eabi toolchain, i.e. from [here](https://developer.arm.com/tools-and-software/open-source-software/developer-tools/gnu-toolchain/downloads)
- [octave](https://www.gnu.org/software/octave/download) with
- [octave-signal](https://octave.sourceforge.io/signal/) extension.

## Quickstart
```
git submodule init
git submodule update
make
```

## Pinout

Resulting PWM outputs are GPIOA8/GPIOA9 for left/right channels,
and by default are set up as open-drain, which is enough for
most trivial case -- directly attached headphones with high enough
(300+ Ohm) resistance, like (some? most?) in-ear plugs.

<img src="img/headset.png" />

Otherwise, you'll probably need proper power stage with
all four GPIOA8/GPIOA9 and complementary GPIOB13*/GPIOB14*
configured as push-pull:
```
make POWERSTAGE=1
```

<img src="img/hbridge.png" />
