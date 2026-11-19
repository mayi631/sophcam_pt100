/* Legacy APP discovery/status notification used by the phone APP. */
#ifndef __JL25_DISCOVERY_H__
#define __JL25_DISCOVERY_H__

#ifdef __cplusplus
extern "C" {
#endif

int jl25_discovery_start(void);
void jl25_discovery_stop(void);
void jl25_discovery_note_peer(int fd);

#ifdef __cplusplus
}
#endif

#endif /* __JL25_DISCOVERY_H__ */
