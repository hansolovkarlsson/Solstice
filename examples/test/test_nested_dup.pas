program TestNestedDup;

{ a duplicate name WITHIN the same nested scope must still be rejected -
  the shadowing test_nested_shadow.pas allows a nested scope to reuse an
  ANCESTOR's name, but not to redeclare its own }
procedure Outer;
    procedure Inner;
        var
            y: integer;
            y: integer;
    begin
        y := 1;
    end;

begin
    Inner;
end;

begin
    Outer;
end.
