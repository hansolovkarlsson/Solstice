program TestPower;
begin
    writeln('power(2,10) = ', power(2, 10));      { 1024 }
    writeln('2 ** 10 = ', 2 ** 10);                { 1024 }
    writeln('power(2.0,0.5) = ', power(2.0, 0.5)); { sqrt(2) = 1.41421 }

    { precedence: ** binds tighter than * }
    writeln('2 * 3 ** 2 = ', 2 * 3 ** 2);          { 2 * 9 = 18, not (2*3)**2=36 }

    { right-associativity: 2 ** 3 ** 2 = 2 ** (3**2) = 2**9 = 512 }
    writeln('2 ** 3 ** 2 = ', 2 ** 3 ** 2);

    { unary minus binds tighter than **, per this compiler's documented rule }
    writeln('-2 ** 2 = ', -2 ** 2);                { (-2)**2 = 4 }
end.
