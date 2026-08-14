program TestDArrTCBadSetLen;
{ A BARE dynamic-array typed constant is immutable, exactly like a
  fixed-array/record typed constant - SetLength on it is rejected. }
const
    X: array of integer = [1, 2];
begin
    SetLength(X, 5);
end.
