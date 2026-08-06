program TestSetBasic;
var
    s: set of 0..9;
    x: integer;
begin
    s := [1, 3, 5];
    writeln(1 in s);   { TRUE }
    writeln(2 in s);   { FALSE }
    x := 5;
    writeln(x in s);   { TRUE }

    s := s + [7];      { union }
    writeln(7 in s);   { TRUE }

    s := s - [3];      { difference }
    writeln(3 in s);   { FALSE }

    s := [1..5];       { a range inside a constructor }
    writeln(1 in s, ' ', 3 in s, ' ', 5 in s, ' ', 6 in s); { TRUE TRUE TRUE FALSE }

    s := [];           { the empty set }
    writeln(0 in s);   { FALSE }
end.
