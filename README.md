## F4UAC -- USB Audio Class headphones on STM32F4

Wait, what ? Again ? *sigh* yeah, again.

## Abstract
Loose implementation of excellent ST appnote [AN5142](https://www.st.com/resource/en/application_note/dm00477514-classd-audio-amplifier-implementation-on-stm32-32bit-arm-cortex-mcus-stmicroelectronics.pdf),
using so-called `Black Pill` [boards](https://github.com/WeActTC/MiniSTM32F4x1).

While overall scheme remains the same, some significant differences exists:
- S16/S24/S32/FLOAT@48kHz and S16/S24@96kHz as input;
- 8x/16x upsampler with 16/32-tap FIR interpolator instead of linear;
- 4th order noise shaper instead of simple error feedback;
- 7bit/768kHz PWM as output.

## Requirements
-  arm-none-eabi toolchain, i.e. from [here](https://developer.arm.com/tools-and-software/open-source-software/developer-tools/gnu-toolchain/downloads)
- [octave](https://www.gnu.org/software/octave/download) with
- [octave-signal](https://octave.sourceforge.io/signal/) extension.

## Quickstart
```
git submodule init
git submodule update
make
```

Precompiled binaries are in bin/ directory

## Pinout

Resulting PWM outputs are GPIOA8/GPIOA9 for left/right channels,
with complementary GPIOB13*/GPIOB14*.
Sample schematics for headphones:

<img src="img/headset.png" />

Another one, sort of 'desktop speakers':

<img src="img/hbridge.png" />
