program TestIntBitwise;
var a, b: integer;
begin
    a := 6;
    b := 3;
    writeln('6 and 3 = ', a and b);   { 2 }
    writeln('6 or 3 = ', a or b);     { 7 }
    writeln('6 xor 3 = ', a xor b);   { 5 }
    writeln('not 0 = ', not a - a);   { placeholder to force eval order safe; real check below }
    writeln('not 5 = ', not 5);       { -6 }
    writeln('not 0 = ', not 0);       { -1 }
    writeln('1 shl 4 = ', 1 shl 4);   { 16 }
    writeln('256 shr 4 = ', 256 shr 4); { 16 }
    writeln('-1 shr 28 = ', -1 shr 28); { logical shift: 15, not -1 }
end.
