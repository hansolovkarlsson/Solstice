program TestSizeofNamedTypes;
type
    TAge = 0..150;
    TColor = (Red, Green, Blue);
begin
    { a hand-written subrange stays 4 bytes, NOT narrowed just because
      its bounds happen to overlap byte's range - only the literal
      byte/shortint/word keyword ever narrows anything }
    writeln(sizeOf(TAge));
    writeln(sizeOf(TColor));
end.
