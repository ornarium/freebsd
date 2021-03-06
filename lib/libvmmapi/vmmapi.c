/*-
 * Copyright (c) 2011 NetApp, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY NETAPP, INC ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL NETAPP, INC OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <machine/specialreg.h>

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include <machine/vmm.h>
#include <machine/vmm_dev.h>

#include "vmmapi.h"

struct vmctx {
	int	fd;
	char	*name;
};

#define	CREATE(x)  sysctlbyname("hw.vmm.create", NULL, NULL, (x), strlen((x)))
#define	DESTROY(x) sysctlbyname("hw.vmm.destroy", NULL, NULL, (x), strlen((x)))

static int
vm_device_open(const char *name)
{
        int fd, len;
        char *vmfile;

	len = strlen("/dev/vmm/") + strlen(name) + 1;
	vmfile = malloc(len);
	assert(vmfile != NULL);
	snprintf(vmfile, len, "/dev/vmm/%s", name);

        /* Open the device file */
        fd = open(vmfile, O_RDWR, 0);

	free(vmfile);
        return (fd);
}

int
vm_create(const char *name)
{

	return (CREATE((char *)name));
}

struct vmctx *
vm_open(const char *name)
{
	struct vmctx *vm;

	vm = malloc(sizeof(struct vmctx) + strlen(name) + 1);
	assert(vm != NULL);

	vm->fd = -1;
	vm->name = (char *)(vm + 1);
	strcpy(vm->name, name);

	if ((vm->fd = vm_device_open(vm->name)) < 0)
		goto err;

	return (vm);
err:
	vm_destroy(vm);
	return (NULL);
}

void
vm_destroy(struct vmctx *vm)
{
	assert(vm != NULL);

	if (vm->fd >= 0)
		close(vm->fd);
	DESTROY(vm->name);

	free(vm);
}

size_t
vmm_get_mem_total(void)
{
	size_t mem_total = 0;
	size_t oldlen = sizeof(mem_total);
	int error;
	error = sysctlbyname("hw.vmm.mem_total", &mem_total, &oldlen, NULL, 0);
	if (error)
		return -1;
	return mem_total;
}

size_t
vmm_get_mem_free(void)
{
	size_t mem_free = 0;
	size_t oldlen = sizeof(mem_free);
	int error;
	error = sysctlbyname("hw.vmm.mem_free", &mem_free, &oldlen, NULL, 0);
	if (error)
		return -1;
	return mem_free;
}

int
vm_get_memory_seg(struct vmctx *ctx, vm_paddr_t gpa, size_t *ret_len)
{
	int error;
	struct vm_memory_segment seg;

	bzero(&seg, sizeof(seg));
	seg.gpa = gpa;
	error = ioctl(ctx->fd, VM_GET_MEMORY_SEG, &seg);
	*ret_len = seg.len;
	return (error);
}

int
vm_setup_memory(struct vmctx *ctx, vm_paddr_t gpa, size_t len, char **mapaddr)
{
	int error;
	struct vm_memory_segment seg;

	/*
	 * Create and optionally map 'len' bytes of memory at guest
	 * physical address 'gpa'
	 */
	bzero(&seg, sizeof(seg));
	seg.gpa = gpa;
	seg.len = len;
	error = ioctl(ctx->fd, VM_MAP_MEMORY, &seg);
	if (error == 0 && mapaddr != NULL) {
		*mapaddr = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED,
				ctx->fd, gpa);
	}
	return (error);
}

char *
vm_map_memory(struct vmctx *ctx, vm_paddr_t gpa, size_t len)
{

	/* Map 'len' bytes of memory at guest physical address 'gpa' */
	return ((char *)mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED,
		     ctx->fd, gpa));
}

int
vm_set_desc(struct vmctx *ctx, int vcpu, int reg,
	    uint64_t base, uint32_t limit, uint32_t access)
{
	int error;
	struct vm_seg_desc vmsegdesc;

	bzero(&vmsegdesc, sizeof(vmsegdesc));
	vmsegdesc.cpuid = vcpu;
	vmsegdesc.regnum = reg;
	vmsegdesc.desc.base = base;
	vmsegdesc.desc.limit = limit;
	vmsegdesc.desc.access = access;

	error = ioctl(ctx->fd, VM_SET_SEGMENT_DESCRIPTOR, &vmsegdesc);
	return (error);
}

int
vm_get_desc(struct vmctx *ctx, int vcpu, int reg,
	    uint64_t *base, uint32_t *limit, uint32_t *access)
{
	int error;
	struct vm_seg_desc vmsegdesc;

	bzero(&vmsegdesc, sizeof(vmsegdesc));
	vmsegdesc.cpuid = vcpu;
	vmsegdesc.regnum = reg;

	error = ioctl(ctx->fd, VM_GET_SEGMENT_DESCRIPTOR, &vmsegdesc);
	if (error == 0) {
		*base = vmsegdesc.desc.base;
		*limit = vmsegdesc.desc.limit;
		*access = vmsegdesc.desc.access;
	}
	return (error);
}

int
vm_set_register(struct vmctx *ctx, int vcpu, int reg, uint64_t val)
{
	int error;
	struct vm_register vmreg;

	bzero(&vmreg, sizeof(vmreg));
	vmreg.cpuid = vcpu;
	vmreg.regnum = reg;
	vmreg.regval = val;

	error = ioctl(ctx->fd, VM_SET_REGISTER, &vmreg);
	return (error);
}

int
vm_get_register(struct vmctx *ctx, int vcpu, int reg, uint64_t *ret_val)
{
	int error;
	struct vm_register vmreg;

	bzero(&vmreg, sizeof(vmreg));
	vmreg.cpuid = vcpu;
	vmreg.regnum = reg;

	error = ioctl(ctx->fd, VM_GET_REGISTER, &vmreg);
	*ret_val = vmreg.regval;
	return (error);
}

int
vm_run(struct vmctx *ctx, int vcpu, uint64_t rip, struct vm_exit *vmexit)
{
	int error;
	struct vm_run vmrun;

	bzero(&vmrun, sizeof(vmrun));
	vmrun.cpuid = vcpu;
	vmrun.rip = rip;

	error = ioctl(ctx->fd, VM_RUN, &vmrun);
	bcopy(&vmrun.vm_exit, vmexit, sizeof(struct vm_exit));
	return (error);
}

static int
vm_inject_event_real(struct vmctx *ctx, int vcpu, enum vm_event_type type,
		     int vector, int error_code, int error_code_valid)
{
	struct vm_event ev;

	bzero(&ev, sizeof(ev));
	ev.cpuid = vcpu;
	ev.type = type;
	ev.vector = vector;
	ev.error_code = error_code;
	ev.error_code_valid = error_code_valid;

	return (ioctl(ctx->fd, VM_INJECT_EVENT, &ev));
}

int
vm_inject_event(struct vmctx *ctx, int vcpu, enum vm_event_type type,
		int vector)
{

	return (vm_inject_event_real(ctx, vcpu, type, vector, 0, 0));
}

int
vm_inject_event2(struct vmctx *ctx, int vcpu, enum vm_event_type type,
		 int vector, int error_code)
{

	return (vm_inject_event_real(ctx, vcpu, type, vector, error_code, 1));
}

int
vm_apicid2vcpu(struct vmctx *ctx, int apicid)
{
	/*
	 * The apic id associated with the 'vcpu' has the same numerical value
	 * as the 'vcpu' itself.
	 */
	return (apicid);
}

int
vm_lapic_irq(struct vmctx *ctx, int vcpu, int vector)
{
	struct vm_lapic_irq vmirq;

	bzero(&vmirq, sizeof(vmirq));
	vmirq.cpuid = vcpu;
	vmirq.vector = vector;

	return (ioctl(ctx->fd, VM_LAPIC_IRQ, &vmirq));
}

int
vm_inject_nmi(struct vmctx *ctx, int vcpu)
{
	struct vm_nmi vmnmi;

	bzero(&vmnmi, sizeof(vmnmi));
	vmnmi.cpuid = vcpu;

	return (ioctl(ctx->fd, VM_INJECT_NMI, &vmnmi));
}

static struct {
	const char	*name;
	int		type;
} capstrmap[] = {
	{ "hlt_exit",		VM_CAP_HALT_EXIT },
	{ "mtrap_exit",		VM_CAP_MTRAP_EXIT },
	{ "pause_exit",		VM_CAP_PAUSE_EXIT },
	{ "unrestricted_guest",	VM_CAP_UNRESTRICTED_GUEST },
	{ 0 }
};

int
vm_capability_name2type(const char *capname)
{
	int i;

	for (i = 0; capstrmap[i].name != NULL && capname != NULL; i++) {
		if (strcmp(capstrmap[i].name, capname) == 0)
			return (capstrmap[i].type);
	}

	return (-1);
}

const char *
vm_capability_type2name(int type)
{
	int i;

	for (i = 0; capstrmap[i].name != NULL; i++) {
		if (capstrmap[i].type == type)
			return (capstrmap[i].name);
	}

	return (NULL);
}

int
vm_get_capability(struct vmctx *ctx, int vcpu, enum vm_cap_type cap,
		  int *retval)
{
	int error;
	struct vm_capability vmcap;

	bzero(&vmcap, sizeof(vmcap));
	vmcap.cpuid = vcpu;
	vmcap.captype = cap;

	error = ioctl(ctx->fd, VM_GET_CAPABILITY, &vmcap);
	*retval = vmcap.capval;
	return (error);
}

int
vm_set_capability(struct vmctx *ctx, int vcpu, enum vm_cap_type cap, int val)
{
	struct vm_capability vmcap;

	bzero(&vmcap, sizeof(vmcap));
	vmcap.cpuid = vcpu;
	vmcap.captype = cap;
	vmcap.capval = val;
	
	return (ioctl(ctx->fd, VM_SET_CAPABILITY, &vmcap));
}

int
vm_assign_pptdev(struct vmctx *ctx, int bus, int slot, int func)
{
	struct vm_pptdev pptdev;

	bzero(&pptdev, sizeof(pptdev));
	pptdev.bus = bus;
	pptdev.slot = slot;
	pptdev.func = func;

	return (ioctl(ctx->fd, VM_BIND_PPTDEV, &pptdev));
}

int
vm_unassign_pptdev(struct vmctx *ctx, int bus, int slot, int func)
{
	struct vm_pptdev pptdev;

	bzero(&pptdev, sizeof(pptdev));
	pptdev.bus = bus;
	pptdev.slot = slot;
	pptdev.func = func;

	return (ioctl(ctx->fd, VM_UNBIND_PPTDEV, &pptdev));
}

int
vm_map_pptdev_mmio(struct vmctx *ctx, int bus, int slot, int func,
		   vm_paddr_t gpa, size_t len, vm_paddr_t hpa)
{
	struct vm_pptdev_mmio pptmmio;

	bzero(&pptmmio, sizeof(pptmmio));
	pptmmio.bus = bus;
	pptmmio.slot = slot;
	pptmmio.func = func;
	pptmmio.gpa = gpa;
	pptmmio.len = len;
	pptmmio.hpa = hpa;

	return (ioctl(ctx->fd, VM_MAP_PPTDEV_MMIO, &pptmmio));
}

int
vm_setup_msi(struct vmctx *ctx, int vcpu, int bus, int slot, int func,
	     int destcpu, int vector, int numvec)
{
	struct vm_pptdev_msi pptmsi;

	bzero(&pptmsi, sizeof(pptmsi));
	pptmsi.vcpu = vcpu;
	pptmsi.bus = bus;
	pptmsi.slot = slot;
	pptmsi.func = func;
	pptmsi.destcpu = destcpu;
	pptmsi.vector = vector;
	pptmsi.numvec = numvec;

	return (ioctl(ctx->fd, VM_PPTDEV_MSI, &pptmsi));
}

int	
vm_setup_msix(struct vmctx *ctx, int vcpu, int bus, int slot, int func,
	      int idx, uint32_t msg, uint32_t vector_control, uint64_t addr)
{
	struct vm_pptdev_msix pptmsix;

	bzero(&pptmsix, sizeof(pptmsix));
	pptmsix.vcpu = vcpu;
	pptmsix.bus = bus;
	pptmsix.slot = slot;
	pptmsix.func = func;
	pptmsix.idx = idx;
	pptmsix.msg = msg;
	pptmsix.addr = addr;
	pptmsix.vector_control = vector_control;

	return ioctl(ctx->fd, VM_PPTDEV_MSIX, &pptmsix);
}

uint64_t *
vm_get_stats(struct vmctx *ctx, int vcpu, struct timeval *ret_tv,
	     int *ret_entries)
{
	int error;

	static struct vm_stats vmstats;

	vmstats.cpuid = vcpu;

	error = ioctl(ctx->fd, VM_STATS, &vmstats);
	if (error == 0) {
		if (ret_entries)
			*ret_entries = vmstats.num_entries;
		if (ret_tv)
			*ret_tv = vmstats.tv;
		return (vmstats.statbuf);
	} else
		return (NULL);
}

const char *
vm_get_stat_desc(struct vmctx *ctx, int index)
{
	static struct vm_stat_desc statdesc;

	statdesc.index = index;
	if (ioctl(ctx->fd, VM_STAT_DESC, &statdesc) == 0)
		return (statdesc.desc);
	else
		return (NULL);
}

int
vm_get_x2apic_state(struct vmctx *ctx, int vcpu, enum x2apic_state *state)
{
	int error;
	struct vm_x2apic x2apic;

	bzero(&x2apic, sizeof(x2apic));
	x2apic.cpuid = vcpu;

	error = ioctl(ctx->fd, VM_GET_X2APIC_STATE, &x2apic);
	*state = x2apic.state;
	return (error);
}

int
vm_set_x2apic_state(struct vmctx *ctx, int vcpu, enum x2apic_state state)
{
	int error;
	struct vm_x2apic x2apic;

	bzero(&x2apic, sizeof(x2apic));
	x2apic.cpuid = vcpu;
	x2apic.state = state;

	error = ioctl(ctx->fd, VM_SET_X2APIC_STATE, &x2apic);

	return (error);
}

/*
 * From Intel Vol 3a:
 * Table 9-1. IA-32 Processor States Following Power-up, Reset or INIT
 */
int
vcpu_reset(struct vmctx *vmctx, int vcpu)
{
	int error;
	uint64_t rflags, rip, cr0, cr4, zero, desc_base, rdx;
	uint32_t desc_access, desc_limit;
	uint16_t sel;

	zero = 0;

	rflags = 0x2;
	error = vm_set_register(vmctx, vcpu, VM_REG_GUEST_RFLAGS, rflags);
	if (error)
		goto done;

	rip = 0xfff0;
	if ((error = vm_set_register(vmctx, vcpu, VM_REG_GUEST_RIP, rip)) != 0)
		goto done;

	cr0 = CR0_NE;
	if ((error = vm_set_register(vmctx, vcpu, VM_REG_GUEST_CR0, cr0)) != 0)
		goto done;

	if ((error = vm_set_register(vmctx, vcpu, VM_REG_GUEST_CR3, zero)) != 0)
		goto done;
	
	cr4 = 0;
	if ((error = vm_set_register(vmctx, vcpu, VM_REG_GUEST_CR4, cr4)) != 0)
		goto done;

	/*
	 * CS: present, r/w, accessed, 16-bit, byte granularity, usable
	 */
	desc_base = 0xffff0000;
	desc_limit = 0xffff;
	desc_access = 0x0093;
	error = vm_set_desc(vmctx, vcpu, VM_REG_GUEST_CS,
			    desc_base, desc_limit, desc_access);
	if (error)
		goto done;

	sel = 0xf000;
	if ((error = vm_set_register(vmctx, vcpu, VM_REG_GUEST_CS, sel)) != 0)
		goto done;

	/*
	 * SS,DS,ES,FS,GS: present, r/w, accessed, 16-bit, byte granularity
	 */
	desc_base = 0;
	desc_limit = 0xffff;
	desc_access = 0x0093;
	error = vm_set_desc(vmctx, vcpu, VM_REG_GUEST_SS,
			    desc_base, desc_limit, desc_access);
	if (error)
		goto done;

	error = vm_set_desc(vmctx, vcpu, VM_REG_GUEST_DS,
			    desc_base, desc_limit, desc_access);
	if (error)
		goto done;

	error = vm_set_desc(vmctx, vcpu, VM_REG_GUEST_ES,
			    desc_base, desc_limit, desc_access);
	if (error)
		goto done;

	error = vm_set_desc(vmctx, vcpu, VM_REG_GUEST_FS,
			    desc_base, desc_limit, desc_access);
	if (error)
		goto done;

	error = vm_set_desc(vmctx, vcpu, VM_REG_GUEST_GS,
			    desc_base, desc_limit, desc_access);
	if (error)
		goto done;

	sel = 0;
	if ((error = vm_set_register(vmctx, vcpu, VM_REG_GUEST_SS, sel)) != 0)
		goto done;
	if ((error = vm_set_register(vmctx, vcpu, VM_REG_GUEST_DS, sel)) != 0)
		goto done;
	if ((error = vm_set_register(vmctx, vcpu, VM_REG_GUEST_ES, sel)) != 0)
		goto done;
	if ((error = vm_set_register(vmctx, vcpu, VM_REG_GUEST_FS, sel)) != 0)
		goto done;
	if ((error = vm_set_register(vmctx, vcpu, VM_REG_GUEST_GS, sel)) != 0)
		goto done;

	/* General purpose registers */
	rdx = 0xf00;
	if ((error = vm_set_register(vmctx, vcpu, VM_REG_GUEST_RAX, zero)) != 0)
		goto done;
	if ((error = vm_set_register(vmctx, vcpu, VM_REG_GUEST_RBX, zero)) != 0)
		goto done;
	if ((error = vm_set_register(vmctx, vcpu, VM_REG_GUEST_RCX, zero)) != 0)
		goto done;
	if ((error = vm_set_register(vmctx, vcpu, VM_REG_GUEST_RDX, rdx)) != 0)
		goto done;
	if ((error = vm_set_register(vmctx, vcpu, VM_REG_GUEST_RSI, zero)) != 0)
		goto done;
	if ((error = vm_set_register(vmctx, vcpu, VM_REG_GUEST_RDI, zero)) != 0)
		goto done;
	if ((error = vm_set_register(vmctx, vcpu, VM_REG_GUEST_RBP, zero)) != 0)
		goto done;
	if ((error = vm_set_register(vmctx, vcpu, VM_REG_GUEST_RSP, zero)) != 0)
		goto done;

	/* GDTR, IDTR */
	desc_base = 0;
	desc_limit = 0xffff;
	desc_access = 0;
	error = vm_set_desc(vmctx, vcpu, VM_REG_GUEST_GDTR,
			    desc_base, desc_limit, desc_access);
	if (error != 0)
		goto done;

	error = vm_set_desc(vmctx, vcpu, VM_REG_GUEST_IDTR,
			    desc_base, desc_limit, desc_access);
	if (error != 0)
		goto done;

	/* TR */
	desc_base = 0;
	desc_limit = 0xffff;
	desc_access = 0x0000008b;
	error = vm_set_desc(vmctx, vcpu, VM_REG_GUEST_TR, 0, 0, desc_access);
	if (error)
		goto done;

	sel = 0;
	if ((error = vm_set_register(vmctx, vcpu, VM_REG_GUEST_TR, sel)) != 0)
		goto done;

	/* LDTR */
	desc_base = 0;
	desc_limit = 0xffff;
	desc_access = 0x00000082;
	error = vm_set_desc(vmctx, vcpu, VM_REG_GUEST_LDTR, desc_base,
			    desc_limit, desc_access);
	if (error)
		goto done;

	sel = 0;
	if ((error = vm_set_register(vmctx, vcpu, VM_REG_GUEST_LDTR, 0)) != 0)
		goto done;

	/* XXX cr2, debug registers */

	error = 0;
done:
	return (error);
}
