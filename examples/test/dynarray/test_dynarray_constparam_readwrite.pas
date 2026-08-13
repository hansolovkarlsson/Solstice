program TestDynArrayConstParamReadWrite;
var
    arr: array of integer;

procedure Foo(const a: array of integer);
begin
    writeln(Length(a));
    writeln(a[0]);
    a[0] := 999; { writing THROUGH a const parameter is shallow, allowed - }
                 { only reassigning a's OWN pointer (SetLength) is blocked }
    writeln(a[0]);
end;

begin
    SetLength(arr, 2);
    arr[0] := 1;
    Foo(arr);
    writeln(arr[0]); { visible here too - const didn't copy the array }
end.
