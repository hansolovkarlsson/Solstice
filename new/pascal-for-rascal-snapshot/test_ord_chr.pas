program TestOrdChr;
var c: char;
begin
    writeln(ord('A'));       { 65 }
    writeln(ord('a'));       { 97 }
    writeln(chr(65));        { A }
    c := chr(97);
    writeln(c);               { a }
    writeln(ord(chr(200)));  { 200 - round trip }

    { #NNN literal syntax }
    c := #65;
    writeln(c);               { A }
    writeln(#97);             { a }
    writeln('Hi' + #33);      { Hi! }
end.
