from initdata import phase0, phase1

templ = '''\
#ifndef T20_INIT
#define T20_INIT

char *phase0[] = {{
\t{0}
}};

char *phase1[] = {{
\t{1}
}};

inline size_t command_length(const char* cmd) {{
\tswitch(cmd[0]) {{
\t\t{cases}
\t\tdefault:
\t\t\treturn 0;
\t}}
}}

#endif // T20_INIT\
'''

def raw_to_c(raw):
    return '\n\t'.join('"%s",' % ''.join("\\x%02x" % char for char in line) for line in raw)

def filter_input(raw):
    return (line for line in raw if line[0] not in (0x25, 0x05))

def find_lengths(raw):
    starts = set(line[0] for line in raw)

    lengths = {}
    for start in starts:
        for line in raw:
            if line[0] == start:
                lengths[start] = len(line)
                break

    return lengths

def lengths_to_cases(lengths):
    format_str = "case 0x%02x:\n\t\t\treturn %s;"
    return '\n\t\t'.join(format_str % (start, length) for start, length in lengths.items())

raw_phases = [raw_to_c(filter_input(init)) for init in phase0, phase1]
cases = lengths_to_cases(find_lengths(phase0 + phase1))

print templ.format(*raw_phases, cases=cases)

