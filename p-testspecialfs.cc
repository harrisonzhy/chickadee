#include "u-lib.hh"

void process_main() {
    printf("Starting testspecialfs (assuming clean file system)...\n");

    // read file
    printf("%s:%d: read...\n", __FILE__, __LINE__);

    int f = sys_open("emerson.txt", OF_READ);
    assert_gt(f, 2);

    char buf[200];
    memset(buf, 0, sizeof(buf));
    ssize_t n = sys_read(f, buf, 200);
    assert_eq(n, 130);
    assert_memeq(buf, "When piped a tiny voice hard by,\n"
                 "Gay and polite, a cheerful cry,\n"
                 "Chic-chicadeedee", 81);

    sys_close(f);

    char first_bigbuf[524];
    char second_bigbuf[524];
    memset(first_bigbuf, 0, sizeof(first_bigbuf));
    memset(second_bigbuf, 0, sizeof(second_bigbuf));

    printf("%s:%d: {read, write} /dev/random...\n", __FILE__, __LINE__);

    // write to `/dev/random`
    f = sys_open("/dev/random", OF_WRITE);
    assert_gt(f, 2);
    n = sys_write(f, buf, 200);
    assert_eq(n, 200);
    n = sys_write(f, first_bigbuf, 524);
    assert_eq(n, 524);
    n = sys_write(f, second_bigbuf, 524);
    assert_eq(n, 524);

    // read `/dev/random`
    int f2 = sys_open("/dev/random", OF_READ);
    assert_gt(f2, 2);
    n = sys_read(f2, buf, 200);
    assert_eq(n, 200);
    n = sys_read(f2, first_bigbuf, 524);
    assert_eq(n, 512);
    n = sys_read(f2, second_bigbuf, 524);
    assert_eq(n, 512);
    
    // buffer contents should be different
    int m = memcmp(first_bigbuf, second_bigbuf, n);
    assert_ne(m, 0);

    sys_close(f);
    sys_close(f2);

    memset(first_bigbuf, 'a', sizeof(first_bigbuf));
    memset(second_bigbuf, 'a', sizeof(second_bigbuf));

    printf("%s:%d: {read, write} /dev/null...\n", __FILE__, __LINE__);
    // write to `/dev/null`
    f = sys_open("/dev/null", OF_WRITE);
    assert_gt(f, 2);
    n = sys_write(f, buf, 200);
    assert_eq(n, 200);
    n = sys_write(f, first_bigbuf, 524);
    assert_eq(n, 524);

    // read `/dev/null`
    f2 = sys_open("/dev/null", OF_READ);
    assert_gt(f2, 2);
    n = sys_read(f2, buf, 200);
    assert_eq(n, 200);
    n = sys_read(f2, first_bigbuf, 524);
    assert_eq(n, 524);

    // buffer contents should be the same (read and write has no effect)
    m = memcmp(first_bigbuf, second_bigbuf, sizeof(first_bigbuf));
    assert_eq(m, 0);

    sys_close(f);
    sys_close(f2);

    // create file
    printf("%s:%d: create...\n", __FILE__, __LINE__);

    f = sys_open("geisel.txt", OF_WRITE);
    assert_lt(f, 0);
    assert_eq(f, E_NOENT);

    f = sys_open("geisel.txt", OF_WRITE | OF_CREATE);
    assert_gt(f, 2);

    n = sys_write(f, "Why, girl, you're insane!\n"
                  "Elephants don't hatch chickadee eggs!\n", 64);
    assert_eq(n, 64);

    sys_close(f);

    // rename `/dev/random`
    printf("%s:%d: rename /dev/{null, random}...\n", __FILE__, __LINE__);

    int s = sys_rename("/dev/null", "not_dev_null");
    assert_eq(s, E_PERM);
    s = sys_rename("/dev/random", "not_dev_random");
    assert_eq(s, E_PERM);

    s = sys_rename("geisel.txt", "not_geisel.txt");

    f = sys_open("geisel.txt", OF_WRITE);
    assert_eq(f, E_NOENT);

    f = sys_open("not_geisel.txt", OF_WRITE);
    assert_gt(f, 2);

    s = sys_rename("not_geisel.txt", "geisel.txt");
    assert_eq(s, 0);

    s = sys_rename("geisel.txt", "/dev/null");
    assert_eq(s, E_PERM);
    s = sys_rename("geisel.txt", "/dev/random");
    assert_eq(s, E_PERM);
    
    s = sys_rename("geisel.txt", "not_geisel.txt");
    assert_eq(s, 0);
    
    // read back
    printf("%s:%d: read created...\n", __FILE__, __LINE__);

    f = sys_open("not_geisel.txt", OF_READ);
    assert_gt(f, 2);

    memset(buf, 0, sizeof(buf));
    n = sys_read(f, buf, 200);
    assert_eq(n, 64);
    assert_memeq(buf, "Why, girl, you're insane!\n"
                 "Elephants don't hatch chickadee eggs!\n", 64);

    s = sys_rename("not_geisel.txt", "geisel.txt");
    assert_eq(s, 0);

    sys_close(f);

    f = sys_open("geisel.txt", OF_READ);
    assert_gt(f, 2);

    sys_close(f);

    // synchronize disk
    printf("%s:%d: sync...\n", __FILE__, __LINE__);

    int r = sys_sync(1);
    assert_ge(r, 0);

    f = sys_open("geisel.txt", OF_READ);
    assert_gt(f, 2);

    memset(buf, 0, sizeof(buf));
    n = sys_read(f, buf, 200);
    assert_eq(n, 64);
    assert_memeq(buf, "Why, girl, you're insane!\n"
                 "Elephants don't hatch chickadee eggs!\n", 64);

    sys_close(f);

    printf("%s:%d: more rename /dev/{null, random}...\n", __FILE__, __LINE__);
    s = sys_rename("/dev/null", "/dev/random");
    assert_eq(s, E_PERM);

    s = sys_rename("/dev/null", "/dev/null");
    assert_eq(s, 0);

    s = sys_rename("/dev/random", "/dev/null");
    assert_eq(s, E_PERM);

    s = sys_rename("/dev/random", "/dev/random");
    assert_eq(s, 0);
    
    s = sys_rename("not_geisel.txt", "emerson.txt");
    assert_lt(s, 0);

    // read & write same file
    printf("%s:%d: interleave open, rename...\n", __FILE__, __LINE__);

    f = sys_open("geisel.txt", OF_READ);
    assert_gt(f, 2);

    s = sys_rename("geisel.txt", "not_geisel.txt");
    assert_eq(s, 0);

    s = sys_rename("not_geisel.txt", "/dev/null");
    assert_lt(s, 0);

    int wf = sys_open("geisel.txt", OF_WRITE);
    assert_lt(wf, 0);

    s = sys_rename("not_geisel.txt", "geisel.txt");
    assert_eq(s, 0);

    wf = sys_open("geisel.txt", OF_WRITE);
    assert_gt(wf, 2);
    assert_ne(wf, f);

    s = sys_rename("geisel.txt", "/dev/random");
    assert_lt(s, 0);
    
    sys_close(wf);

    printf("%s:%d: rename same...\n", __FILE__, __LINE__);
    s = sys_rename("geisel.txt", "geisel.txt");
    assert_eq(s, 0);
    s = sys_rename("geisel.txt", "emerson.txt");
    assert_lt(s, 0);
    s = sys_rename("/dev/random", "/dev/random");
    assert_eq(s, 0);
    s = sys_rename("geisel.txt", "not_geisel.txt");
    assert_eq(s, 0);
    s = sys_rename("geisel.txt", "not_geisel.txt");
    assert_lt(s, 0);
    s = sys_rename("not_geisel.txt", "not_geisel.txt");
    assert_eq(s, 0);
    s = sys_rename("not_geisel.txt", "geisel.txt");
    assert_eq(s, 0);
    s = sys_rename("/dev/none", "/dev/none");
    assert_eq(s, 0);
    s = sys_rename("geisel.txt", "emerson.txt");
    assert_lt(s, 0);

    wf = sys_open("geisel.txt", OF_WRITE);
    assert_gt(wf, 2);
    assert_ne(wf, f);

    memset(buf, 0, sizeof(buf));
    n = sys_read(f, buf, 4);
    assert_eq(n, 4);
    assert_memeq(buf, "Why,", 4);

    n = sys_write(wf, "Am I scaring you tonight?", 25);
    assert_eq(n, 25);

    memset(buf, 0, sizeof(buf));
    n = sys_read(f, buf, 25);
    assert_eq(n, 25);
    assert_memeq(buf, " scaring you tonight?\nEle", 25);

    n = sys_write(wf, "!", 1);
    assert_eq(n, 1);

    memset(buf, 0, sizeof(buf));
    n = sys_read(f, buf, 5);
    assert_eq(n, 5);
    assert_memeq(buf, "phant", 5);

    sys_close(f);
    sys_close(wf);


    printf("testspecialfs succeeded!\n");
    sys_exit(0);
}
