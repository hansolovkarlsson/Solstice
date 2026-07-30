program TestStrMutMultichar;
var s, t: string;
begin
    s := 'hi';
    t := 'xy';
    s[1] := t;
    writeln(s);
end.
