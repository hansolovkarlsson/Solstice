program TestStringBasic;
var s: string;
begin
    s := 'Hello, World!';
    writeln('length: ', length(s));               { 13 }
    writeln('pos: ', pos('World', s));             { 8 }
    writeln('pos not found: ', pos('xyz', s));     { 0 }
    writeln('copy: ', copy(s, 8, 5));               { World }
    writeln('copy clamped: ', copy(s, 8, 100));     { World! }
    writeln('copy oob start: ', copy(s, 100, 5));   { (empty) }
    writeln('mid: ', mid(s, 1, 5));                 { Hello }
    writeln('left: ', left(s, 5));                  { Hello }
    writeln('right: ', right(s, 6));                { World! }
    writeln('inpos: ', inpos('l', s));               { 3 }
end.
