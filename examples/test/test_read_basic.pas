program TestReadBasic;
var
    a, b, c: integer;
begin
    { 'read' does NOT consume the rest of the line - three reads can
      pull three values off one line. Only the LAST one uses 'readln',
      which does flush to the next line afterward. }
    read(a);
    read(b);
    readln(c);
    writeln('a=', a, ' b=', b, ' c=', c);
end.
