program TestFinallyNestedCall;
var result: integer;

procedure Deep;
begin
    raise 'deep failure';
end;

procedure Middle;
begin
    Deep;
end;

begin
    result := 0;
    try
        try
            Middle;
        finally
            { cleanup must run with a correctly-restored frame/local
              state despite the exception originating two calls deep }
            result := result + 10;
            writeln('cleanup ran, result = ', result);
        end;
    except
        writeln('caught: ', ExceptMessage);
    end;
    result := result + 1;
    writeln('final result = ', result);
end.
