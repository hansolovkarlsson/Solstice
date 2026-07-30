program TestWriteFieldNegWidth;
var w: integer;
begin
    w := -5;
    writeln('[', 42:w, ']');   { negative width left-justifies: [42   ] }
end.
