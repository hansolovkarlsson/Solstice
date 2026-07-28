program FuncNonLastArg;
var x: integer;

function sq(n: integer): integer;
begin
    sq := n * n;
end;

function addOne(n: integer): integer;
begin
    addOne := n + 1;
end;

begin
    { call as first of two writeln args }
    writeln(sq(3), ' and ', sq(4));
    { call as first of three writeln args }
    writeln(sq(2), ' ', sq(3), ' ', sq(4));
    { call as non-last argument to ANOTHER call }
    x := addOne(sq(3));
    writeln('addOne(sq(3)) = ', x);
    { nested calls as first arg of arithmetic }
    writeln(sq(2) + sq(3));
end.
