program TestStrnumInttostr;
begin
    writeln('positive: ', IntToStr(42));
    writeln('negative: ', IntToStr(-17));
    writeln('zero: ', IntToStr(0));
    writeln('concat: ' + IntToStr(100) + ' items');
end.
