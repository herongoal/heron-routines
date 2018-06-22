TARGET=libservice_core.a
OBJS= service.o routine.o log_routine.o ipc_routine.o tcp_service_routine.o hybrid_list.o \
	tcp_passive_session_routine.o udp_session_context.o udp_service_routine.o \
	active_udp_session_routine.o \
	udp_define.o active_tcp_session_routine.o routine_proxy.o log_api.o file_writer.o service_define.o


CC=g++
CC_FLAGS:=-Wall -g -D_REENTRANT

LIB_PATH=-lpthread

all:$(TARGET)

%.o:%.cc
	$(CC) $(CC_FLAGS) -c $< -o $@ $(LIB_PATH)

$(TARGET):$(OBJS)
	rm -f $@
	ar cr $@ $(OBJS)
	rm -f $(OBJS)

clean:
	rm -f $(OBJS) $(TARGET)
