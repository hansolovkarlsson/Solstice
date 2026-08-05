program TestNestedBasic;

{ one level of nesting: Bump reads and writes Outer's own local x,
  neither declared as a parameter nor passed explicitly }
procedure Outer;
    var x: integer;

    procedure Bump;
    begin
        x := x + 1;
    end;

begin
    x := 10;
    Bump;
    Bump;
    Bump;
    writeln('x=', x);
end;

begin
    Outer;
end.
