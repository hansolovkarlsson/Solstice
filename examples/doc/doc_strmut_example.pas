program DocStrMutExample;
var s, t: string;
begin
    s := 'Hello';
    t := 'Hello';   { t shares the same pooled string entry as s }
    s[1] := 'J';
    writeln('s: ', s);   { Jello }
    writeln('t: ', t);   { Hello - unaffected }
end.
