program TestProcParamForwarding;

function Increment(n: integer): integer;
begin
    Increment := n + 1;
end;

function CallIt(function f(n: integer): integer; v: integer): integer;
begin
    CallIt := f(v);
end;

function Relay(function f(n: integer): integer; v: integer): integer;
begin
    Relay := CallIt(f, v);
end;

begin
    writeln(Relay(Increment, 41)); { 42 }
end.
