# Copyright (c) 2023, Kry10 Limited. All rights reserved.
#
# SPDX-License-Identifier: LicenseRef-Kry10

# Create kos_logger.so in the priv/ folder of the application.
$(MIX_COMPILE_PATH)/../priv/kos_msg_nif.so: c_src/kos_msg_nif.c
	mkdir -p $(MIX_COMPILE_PATH)/../priv/
	$(CC) $(KOS_RUNTIME_CFLAGS) -fPIC -I$(ERL_EI_INCLUDE_DIR) $^ -shared -o $@
