program TestForInEnumSet;
type
    TColor = (Red, Green, Blue);
var
    s: set of TColor;
    x: integer;
begin
    s := [Red, Blue];
    { the loop variable must be integer - a set's members are always
      raw ordinals during 'for in' iteration, regardless of what
      ordinal type the set was originally declared over (see
      docs/LANGUAGE.md#goto-and-labels' sibling section on sets) }
    for x in s do
        writeln(x);
end.
