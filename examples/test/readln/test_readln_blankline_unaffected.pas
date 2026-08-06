program TestReadlnBlanklineUnaffected;
{ Regression test for the multi-target flush fix (see
  test_readln_multitarget_flush.pas): standalone string reads, one per
  readln call, must still correctly read a genuinely blank line as an
  empty string - not have it silently skipped. Expected input:
  "first\n\nthird\n" - expected output: [first][][third] }
var
    s1, s2, s3: string;
begin
    readln(s1);
    readln(s2);
    readln(s3);
    writeln('[', s1, '][', s2, '][', s3, ']');
end.
