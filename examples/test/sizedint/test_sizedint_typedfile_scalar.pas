program TestSizedintTypedfileScalar;
var
    fb: file of byte;
    fw: file of word;
    fs: file of shortint;
    b: byte;
    w: word;
    s: shortint;
begin
    assign(fb, '/tmp/ouroboros_test_sizedint_typedfile_scalar_b.bin');
    rewrite(fb);
    b := 250; write(fb, b);
    b := 5; write(fb, b);
    close(fb);
    reset(fb);
    writeln('fb records = ', filesize(fb));
    read(fb, b); writeln('fb[0] = ', b);
    close(fb);

    assign(fw, '/tmp/ouroboros_test_sizedint_typedfile_scalar_w.bin');
    rewrite(fw);
    w := 60000; write(fw, w);
    close(fw);
    reset(fw);
    read(fw, w); writeln('fw[0] = ', w);
    close(fw);

    assign(fs, '/tmp/ouroboros_test_sizedint_typedfile_scalar_s.bin');
    rewrite(fs);
    s := -100; write(fs, s);
    close(fs);
    reset(fs);
    read(fs, s); writeln('fs[0] = ', s);
    close(fs);
end.
