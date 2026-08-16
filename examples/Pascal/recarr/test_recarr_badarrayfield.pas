program TestRecarrBadarrayfield;
{ A record type used as an array's element type must not itself have an
  array-typed field - that would need a variable stride per field
  (each element would need its own copy of the array field's storage),
  which this feature's fixed-stride design doesn't support. Must be a
  clear Compile Error. }
type
    TBucket = record
        items: array[1..3] of integer;
    end;
var
    buckets: array[1..2] of TBucket;
begin
end.
