#ifndef SHELLCOMMANDS_H
#define SHELLCOMMANDS_H

#ifdef __cplusplus
extern "C" {
#endif

int cmd_do_measurement(int argc, char **argv);
int cmd_provision(int argc, char **argv);
int cmd_ec_cmd(int argc, char **argv);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* end of include guard: SHELLCOMMANDS_H */
