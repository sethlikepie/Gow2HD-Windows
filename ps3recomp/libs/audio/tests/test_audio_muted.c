/*
 * PS3_MUTE: contrato de quem decide se o audio sai.
 *
 * Defeito que isto apanha: cada sitio fazia `getenv("PS3_MUTE") ? 1 : 0`, por
 * isso `PS3_MUTE=0` -- escrito por quem QUER som -- deixava o jogo mudo. O
 * `jogar_g2.sh` liga o som assim, e sem esta regra o launcher nao funcionava.
 *
 * A funcao memoriza a resposta (e' lida por bloco de audio), logo cada caso
 * corre num processo seu: o teste reexecuta-se a si proprio com o env posto.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

int ps3_audio_muted(void);
void ps3_audio_set_runtime_mute(int muted);
int ps3_audio_output_silenced(void);
const float* ps3_audio_gate_block(const float* mixed, float* scratch, size_t floats);

int main(int argc, char** argv)
{
    if (argc > 1) {                 /* filho: um caso, um processo */
        if (strcmp(argv[1], "runtime") == 0) {
            float mixed[4] = { 0.5f, -0.5f, 0.25f, -0.25f };
            float scratch[4] = { 9.0f, 9.0f, 9.0f, 9.0f };
            const int env_muted = atoi(argv[2]);
            if (ps3_audio_output_silenced() != env_muted) return 1;
            if (ps3_audio_gate_block(mixed, scratch, 4) != (env_muted ? scratch : mixed)) return 2;
            ps3_audio_set_runtime_mute(1);
            if (!ps3_audio_output_silenced()) return 3;
            const float* out = ps3_audio_gate_block(mixed, scratch, 4);
            if (out != scratch) return 4;
            for (int i = 0; i < 4; ++i) if (out[i] != 0.0f) return 5;
            ps3_audio_set_runtime_mute(0);
            if (ps3_audio_output_silenced() != env_muted) return 6; /* PS3_MUTE still wins */
            return 0;
        }
        const int want = atoi(argv[2]);
        const int got = ps3_audio_muted();
        if (got != want) {
            fprintf(stderr, "FALHOU: PS3_MUTE=%s -> mudo=%d, esperado %d\n",
                    getenv("PS3_MUTE") ? getenv("PS3_MUTE") : "(nao definida)", got, want);
            return 1;
        }
        return 0;
    }

    static const struct { const char* value; int muted; } cases[] = {
        { NULL, 0 },        /* sem a variavel: som */
        { "",   0 },        /* vazia: som (nao e' um pedido de mutar) */
        { "0",  0 },        /* explicitamente desligado: som */
        { "1",  1 },
        { "2",  1 },
        { "yes", 1 },
    };
    int fails = 0;
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        char want[8];
        snprintf(want, sizeof want, "%d", cases[i].muted);
        if (cases[i].value) setenv("PS3_MUTE", cases[i].value, 1);
        else                unsetenv("PS3_MUTE");
        const pid_t pid = fork();
        assert(pid >= 0);
        if (pid == 0) {
            char* args[] = { argv[0], (char*)"caso", want, NULL };
            execv(argv[0], args);
            _exit(127);
        }
        int st = 0;
        while (waitpid(pid, &st, 0) < 0) { }
        if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) fails++;
    }
    static const struct { const char* value; const char* muted; } runtime_cases[] = {
        { NULL, "0" }, { "1", "1" },
    };
    for (unsigned i = 0; i < sizeof runtime_cases / sizeof runtime_cases[0]; i++) {
        if (runtime_cases[i].value) setenv("PS3_MUTE", runtime_cases[i].value, 1);
        else                        unsetenv("PS3_MUTE");
        const pid_t pid = fork();
        assert(pid >= 0);
        if (pid == 0) {
            char* args[] = { argv[0], (char*)"runtime", (char*)runtime_cases[i].muted, NULL };
            execv(argv[0], args);
            _exit(127);
        }
        int st = 0;
        while (waitpid(pid, &st, 0) < 0) { }
        if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) fails++;
    }
    printf("test_audio_muted: %s\n", fails ? "FALHOU" : "PASS");
    return fails ? 1 : 0;
}
