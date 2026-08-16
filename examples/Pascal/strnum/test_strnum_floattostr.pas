program TestStrnumFloattostr;
begin
    writeln(FloatToStr(19.9));
    writeln(FloatToStr(4));           { integer argument, auto-widened }
    writeln(FloatToStr(1000000.0));   { switches to scientific notation under %.6g }
end.
