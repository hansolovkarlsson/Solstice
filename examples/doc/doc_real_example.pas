program DocRealExample;
var
    price: real;
    quantity: integer;
    total: real;
begin
    price := 2.5;
    quantity := 3;
    total := price * quantity;
    writeln('total: ', total);

    writeln('5 / 2 = ', 5 / 2);
    writeln('trunc(total) = ', trunc(total));
    writeln('round(2.6) = ', round(2.6));
end.
