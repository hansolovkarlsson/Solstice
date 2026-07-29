program SlashSlash;
var
    x: real; // trailing comment
begin
    // this whole line is a comment
    x := 5; // another one
    { block comments still work too }
    writeln(x); // should print 5
    x := x / 1; // division operator must still work despite // meaning comment
    writeln(x);
end.
