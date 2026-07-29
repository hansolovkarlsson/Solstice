program LocalArrayRecursion;
var depth: integer;

procedure recurse(n: integer);
var
    marker: array[1..1] of integer;
begin
    if n = 3 then begin
        marker[1] := 999;   { deepest call sets it }
        writeln('deepest sets marker[1] = ', marker[1]);
    end else begin
        recurse(n + 1);
        { because the array is SHARED (not per-call), this sees the
          deepest call's write, even though THIS call never set it }
        writeln('call n=', n, ' sees marker[1] = ', marker[1]);
    end;
end;

begin
    recurse(1);
end.
