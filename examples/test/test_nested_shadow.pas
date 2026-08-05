program TestNestedShadow;

{ Inner declares its own 'x', which must shadow Outer's 'x' - not get
  rejected as a duplicate declaration (standard Pascal allows shadowing;
  only a duplicate WITHIN the same scope is an error - see
  test_nested_shadow_dup.pas for that negative case) }
procedure Outer;
    var x: integer;

    procedure Inner;
        var x: integer;
    begin
        x := 99;
        writeln('inner x=', x);
    end;

begin
    x := 1;
    Inner;
    writeln('outer x=', x);
end;

begin
    Outer;
end.
