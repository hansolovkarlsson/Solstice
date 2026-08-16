program TestClassTagInherit;
type
    TShape = class
        name: integer;
    end;
    TCircle = class(TShape)
        radius: real;
    end;
var
    c: TCircle;
begin
    { Confirms an inherited field plus the subclass's own field both
      still read/write correctly with the tag slot reserved ahead of
      them - the tag itself is written using TCircle's own class ID
      (not TShape's), independently verified via desole disassembly
      when this feature was built. }
    new(c);
    c.name := 7;
    c.radius := 2.0;
    writeln(c.name, ' ', c.radius);
    dispose(c);
end.
