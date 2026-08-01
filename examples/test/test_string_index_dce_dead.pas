program TestStringIndexDceDead;
{ 'dead' is never read anywhere - but 's[i] := val;' must NOT be
  eliminated by dead-code elimination even so: it bounds-checks i
  against s's actual length at runtime (a Runtime Error if out of
  range), the same observable side effect an array-element assignment's
  index has. This program must still abort at 'dead[99]' even though
  'dead' itself is otherwise completely unused. }
var
    used: string;
    dead: string;
    x: string;
begin
    used := 'hello';
    dead := 'hi';
    dead[99] := 'x';
    x := used;
    writeln(x);
end.
