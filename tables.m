#!/usr/bin/octave -qf

pkg load signal;

function fput_array_f(fd, varname, array)
  fputs(fd, [ "const float ", varname, "[] = {\n" ]);
  fprintf(fd, ["\t%.8ff,\n"], array);
  fputs(fd, "};\n\n");
endfunction

av = argv();
fd = fopen(av{1}, "w");

%---------------------------------------------------------------
N=48; F=8;
fput_array_f(fd, "hc", 10^(-1/20) * 8 * fir1(N-1, 1/F));

%---------------------------------------------------------------
VOLSTEPS=31;
fput_array_f(fd, "vl", 10 .^ (-[0:VOLSTEPS] / 20));
