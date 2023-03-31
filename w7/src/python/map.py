import string

def mapfunc(in_file):

    fobj = open(in_file, "r", encoding="utf-8")
    kv_pair = dict()

    for line in fobj:
        line = line.strip()
        line = line.lower()
        # remove punctuation https://www.w3schools.com/python/ref_string_maketrans.asp
        # replace punctuation with spaces https://stackoverflow.com/a/43934982
        line = line.translate(line.maketrans(string.punctuation, ' '*len(string.punctuation)))

        words = line.split(' ')
        for word in words:
            if word == '':
                continue
            if word in kv_pair:
                kv_pair[word] = kv_pair[word] + 1
            else:
                kv_pair[word] = 1

    fobj.close()

    return kv_pair