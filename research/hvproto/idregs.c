/* Print the ID registers a Hypervisor.framework vCPU exposes (no VM is created). */
#include <Hypervisor/Hypervisor.h>
#include <stdio.h>
int main(void)
{
    static const struct { const char *name; hv_feature_reg_t reg; } map[] = {
        { "PFR0", HV_FEATURE_REG_ID_AA64PFR0_EL1 }, { "ISAR0", HV_FEATURE_REG_ID_AA64ISAR0_EL1 },
        { "ISAR1", HV_FEATURE_REG_ID_AA64ISAR1_EL1 }, { "ISAR2", HV_FEATURE_REG_ID_AA64ISAR2_EL1 },
        { "MMFR0", HV_FEATURE_REG_ID_AA64MMFR0_EL1 }, { "MMFR2", HV_FEATURE_REG_ID_AA64MMFR2_EL1 },
        { "CTR", HV_FEATURE_REG_CTR_EL0 } };
    hv_vcpu_config_t config = hv_vcpu_config_create();
    for (unsigned i = 0; i < sizeof(map) / sizeof(map[0]); i++)
    {
        uint64_t v = 0;
        hv_return_t r = hv_vcpu_config_get_feature_reg( config, map[i].reg, &v );
        printf( "%-6s %016llx %s\n", map[i].name, (unsigned long long)v, r ? "(error)" : "" );
    }
    return 0;
}
