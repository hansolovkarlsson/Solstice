program TestSetParam;
var
    g: set of 0..9;

procedure ShowMembership(s: set of 0..9; x: integer);
begin
    writeln(x, ' in s: ', x in s);
end;

function MakeSet(a, b: integer): set of 0..9;
begin
    MakeSet := [a, b];
end;

begin
    g := [2, 4, 6];
    ShowMembership(g, 4); { TRUE }
    ShowMembership(g, 5); { FALSE }

    g := MakeSet(2, 7);
    writeln(2 in g, ' ', 7 in g, ' ', 3 in g); { TRUE TRUE FALSE }
end.
