#!/usr/bin/octave -qf

pkg load signal;

function xqs = qs(x, q)
  xmax = max(x);
  xqs = floor((x/xmax) * 2^q) / 2^q;
  xqs = xqs/sum(xqs);
  xqs = floor(xqs * 2^q);
endfunction

function fput_array(fd, varname, ctype, array)
  fputs(fd, [ "const " ctype, " ", varname, "[] = {\n" ]);
  fprintf(fd, ["\t(", ctype, ")%12d,\n"], array);
  fputs(fd, "};\n\n");
endfunction

function fput_array_f(fd, varname, array)
  fputs(fd, [ "const float ", varname, "[] = {\n" ]);
  fprintf(fd, ["\t%.8ff,\n"], array);
  fputs(fd, "};\n\n");
endfunction

av = argv();
fd = fopen(av{1}, "w");

%---------------------------------------------------------------
N=48; F=8;
fput_array_f(fd, "hc", 0.9982 * 8 * fir1(N-1, 1/F));

%---------------------------------------------------------------
VOLSTEPS=31;
fput_array_f(fd, "vl", 10 .^ (-[0:VOLSTEPS] / 20));

%---------------------------------------------------------------
N=4; F=8; Q=31;
fput_array(fd, "hcx", "int32_t", qs(fir1(N-1, 1/F), Q));
