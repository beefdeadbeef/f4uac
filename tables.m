#!/usr/bin/octave -qf

pkg load signal;

%---------------------------------------------------------------
HEADER = "\
/*\n\
 */\n\
\n\
#define VOLSTEPS\t\t\t%d\n\
extern const float scale[VOLSTEPS];\n\
extern const int16_t db[VOLSTEPS];\n\
\n\
#define NUMTAPS_SR\t\t\t%d\n\
#define UPSAMPLE_SHIFT_SR\t\t%d\n\
extern const float hc_sr[NUMTAPS_SR];\n\
\n\
#define NUMTAPS_DR\t\t\t%d\n\
#define UPSAMPLE_SHIFT_DR\t\t%d\n\
extern const float hc_dr[NUMTAPS_DR];\n\
\n\
";

BODY = "\
/*\n\
 */\n\
\n\
#include <stdint.h>\n\
\n\
const float scale[] = {\n\
%s\
};\n\
\n\
const int16_t db[] = {\n\
%s\
};\n\
\n\
const float hc_sr[] = {\n\
%s\
};\n\
\n\
const float hc_dr[] = {\n\
%s\
};\n\
\n\
";
%---------------------------------------------------------------
function o = retap(u, v)

  o = [];
  n = length(v);
  ph = n / u;

  for i = [u:-1:1]
    t = i;
    for k = [ph:-1:1]
      o = [o, v(t)]; t = t + u;
    endfor
  endfor

endfunction

function s = carray(v)
  s = sprintf(["\t%.8ff,\n"], v);
endfunction

function s = sfir(e, n)
  f = 2^e;
  s = carray(retap(f, f * fir1(n-1, 1/f)));
endfunction

function s = sample(n, fs)
  s = carray(sin(2*pi*[0:n-1]*1000/fs));
endfunction

%---------------------------------------------------------------
VOLSTEPS=61;
ATTN = 10^(-3/20);

function x = db(x)
  x = 20 * log10(x);
endfunction

function x = vol(x)
  x = exp(log(1000) * x) / 1000;
endfunction

VOL = vol([1:-1/(VOLSTEPS-1):0]);
%---------------------------------------------------------------

NUMTAPS_SR = 48;
NUMTAPS_DR = 24;
UPSAMPLE_SHIFT_SR = 3;
UPSAMPLE_SHIFT_DR = 2;
% ---------------------------------------
% UPSAMPLE : NUMTAPS : PHASELEN : BACKLOG
% ---------------------------------------
%        8 :      16 :        2 :       2
%       16 :      32 :        2 :       2
% ---------------------------------------
%        8 :      24 :        3 :       4
%       16 :      48 :        3 :       4
% ---------------------------------------
%        8 :      32 :        4 :       6
%       16 :      64 :        4 :       6
% ---------------------------------------
%        8 :      48 :        6 :      10
%       16 :      96 :        6 :      10
% ---------------------------------------

av = argv();
switch (substr(av{1}, -1))
  case "h"
    fd = fopen(av{1}, "w");
    fprintf(fd, HEADER,
            VOLSTEPS,
            NUMTAPS_SR, UPSAMPLE_SHIFT_SR,
            NUMTAPS_DR, UPSAMPLE_SHIFT_DR);
    fclose(fd);
  case "c"
    fd = fopen(av{1}, "w");
    fprintf(fd, BODY,
            carray(VOL),
            sprintf(["\t%8d,\n"], fix(256 * db(VOL))),
            sfir(UPSAMPLE_SHIFT_SR,NUMTAPS_SR),
            sfir(UPSAMPLE_SHIFT_DR,NUMTAPS_DR));
    fclose(fd);
endswitch
