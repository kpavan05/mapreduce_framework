import string
def reducefunc(in_file):
    fobj = open(in_file, "r", encoding="utf-8")
    kv_pair = dict()
    
    lines = sorted(fobj.readlines())

    for line in lines:
        key, value = line.rsplit(':')
        key = key.translate(line.maketrans("", "", string.punctuation))
        
        if key in kv_pair:
            kv_pair[key] += int(value)
        else:
            kv_pair[key] = int(value)
    
    fobj.close()
    return kv_pair