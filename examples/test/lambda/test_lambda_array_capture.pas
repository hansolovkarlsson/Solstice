program TestLambdaArrayCapture;

{ A lambda reading an enclosing procedure's local array - still allowed
  (a local array is already a hidden global under the hood, see
  test_nested_array.pas), NOT rejected by the capture check.
  Sum of squares 1..5 = 1+4+9+16+25 = 55. No loop inside the lambda body
  itself (a lambda has no local var section in v1, so no loop counter
  to declare) - a lambda literal has no children.
}

type
    TSummer = function: integer;

procedure Outer;
    var
        nums: array[1..5] of integer;
        i: integer;
        summer: TSummer;
begin
    for i := 1 to 5 do
        nums[i] := i * i;

    summer := function: integer
    begin
        exit(nums[1] + nums[2] + nums[3] + nums[4] + nums[5]);
    end;

    writeln(summer());
end;

begin
    Outer;
end.
