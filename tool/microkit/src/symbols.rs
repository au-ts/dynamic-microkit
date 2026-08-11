//
// Copyright 2025, UNSW
//
// SPDX-License-Identifier: BSD-2-Clause
//

use std::{cmp::min, collections::HashMap, fs, path::Path};

use crate::{
    elf::ElfFile,
    sdf::{self, SysMemoryRegion, SystemDescription},
    sel4::{Arch, Config},
    util::{monitor_serialise_names, monitor_serialise_u64_vec},
    MAX_PDS, MAX_VMS, PD_MAX_NAME_LENGTH, VM_MAX_NAME_LENGTH,
};

const SYMBOL_BUNDLE_MAGIC: &[u8; 8] = b"MKTSYMB\0";

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SymbolPatch {
    pub symbol: String,
    pub data: Vec<u8>,
    /// This is used for SDF setvars, otherwise None
    pub expected_size: Option<u64>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SymbolBundle {
    pub pd_name: String,
    pub patches: Vec<SymbolPatch>,
}

impl SymbolBundle {
    pub fn serialise(&self) -> Result<Vec<u8>, String> {
        let pd_name = self.pd_name.as_bytes();
        let pd_name_len: u16 = pd_name.len() as u16;
        debug_assert!(usize::from(pd_name_len) <= PD_MAX_NAME_LENGTH);

        // Should we restrict the number of setvars?
        let symbol_cnt: u32 = self
            .patches
            .len()
            .try_into()
            .map_err(|_| format!("too many symbol patches for PD '{}'", self.pd_name))?;

        let mut output = Vec::new();

        // Serialise a header for the symbol bundle
        output.extend_from_slice(SYMBOL_BUNDLE_MAGIC);
        output.extend_from_slice(&pd_name_len.to_le_bytes());
        output.extend_from_slice(&symbol_cnt.to_le_bytes());
        output.extend_from_slice(pd_name);

        for patch in &self.patches {
            // We trust the sdf parser to ensure these variables are legal
            let symbol = patch.symbol.as_bytes();
            let symbol_len = symbol.len() as u16;
            let data_len = patch.data.len() as u32;

            let (setvar_flag, expected_size) = match patch.expected_size {
                Some(size) => (1u16, size),
                None => (0u16, 0),
            };

            output.extend_from_slice(&symbol_len.to_le_bytes());
            output.extend_from_slice(&setvar_flag.to_le_bytes());
            output.extend_from_slice(&data_len.to_le_bytes());
            output.extend_from_slice(&expected_size.to_le_bytes());
            output.extend_from_slice(symbol);
            output.extend_from_slice(&patch.data);
        }

        Ok(output)
    }
}

/// Patch all the required symbols in the Monitor and children PDs according to
/// the Microkit's requirements. For any PD with sym_emit=true, also return the
/// same resolved patch plan for emission as an external build artifact.
pub fn patch_symbols(
    kernel_config: &Config,
    pd_elf_files: &mut [ElfFile],
    system: &SystemDescription,
) -> Result<Vec<SymbolBundle>, String> {
    // *********************************
    // Step 1. Write ELF symbols in the monitor.
    // *********************************
    let monitor_elf = pd_elf_files.last_mut().unwrap();

    let pd_names: Vec<String> = system
        .protection_domains
        .iter()
        .map(|pd| pd.name.clone())
        .collect();
    monitor_elf
        .write_symbol(
            "pd_names_len",
            &system.protection_domains.len().to_le_bytes(),
        )
        .unwrap();
    monitor_elf
        .write_symbol(
            "pd_names",
            &monitor_serialise_names(&pd_names, MAX_PDS, PD_MAX_NAME_LENGTH),
        )
        .unwrap();

    let vm_names: Vec<String> = system
        .protection_domains
        .iter()
        .filter(|pd| pd.virtual_machine.is_some())
        .flat_map(|pd_with_vm| {
            let vm = pd_with_vm.virtual_machine.as_ref().unwrap();
            let num_vcpus = vm.vcpus.len();
            std::iter::repeat_n(vm.name.clone(), num_vcpus)
        })
        .collect();

    let vm_names_len = match kernel_config.arch {
        Arch::Aarch64 | Arch::Riscv64 => vm_names.len(),
        // VM on x86 doesn't have a separate TCB.
        Arch::X86_64 => 0,
    };
    monitor_elf
        .write_symbol("vm_names_len", &vm_names_len.to_le_bytes())
        .unwrap();
    monitor_elf
        .write_symbol(
            "vm_names",
            &monitor_serialise_names(&vm_names, MAX_VMS, VM_MAX_NAME_LENGTH),
        )
        .unwrap();

    let mut pd_stack_bottoms: Vec<u64> = Vec::new();
    for pd in system.protection_domains.iter() {
        let cur_stack_vaddr = kernel_config.pd_stack_bottom(pd.stack_size);
        pd_stack_bottoms.push(cur_stack_vaddr);
    }
    monitor_elf
        .write_symbol(
            "pd_stack_bottom_addrs",
            &monitor_serialise_u64_vec(&pd_stack_bottoms),
        )
        .unwrap();

    // *********************************
    // Step 2. Write ELF symbols for each PD
    // *********************************
    let mut mr_name_to_desc: HashMap<&String, &SysMemoryRegion> = HashMap::new();
    for mr in system.memory_regions.iter() {
        mr_name_to_desc.insert(&mr.name, mr);
    }

    let mut bundles = Vec::new();

    for (pd_global_idx, pd) in system.protection_domains.iter().enumerate() {
        let name = pd.name.as_bytes();
        let name_length = min(name.len(), PD_MAX_NAME_LENGTH);

        let mut patches = vec![
            SymbolPatch {
                symbol: "microkit_name".to_string(),
                data: name[..name_length].to_vec(),
                expected_size: None,
            },
            SymbolPatch {
                symbol: "microkit_passive".to_string(),
                data: vec![pd.passive as u8],
                expected_size: None,
            },
        ];

        let mut notification_bits: u64 = 0;
        let mut pp_bits: u64 = 0;
        for channel in system.channels.iter() {
            if channel.end_a.pd == pd_global_idx {
                if channel.end_a.notify {
                    notification_bits |= 1 << channel.end_a.id;
                }
                if channel.end_a.pp {
                    pp_bits |= 1 << channel.end_a.id;
                }
            }
            if channel.end_b.pd == pd_global_idx {
                if channel.end_b.notify {
                    notification_bits |= 1 << channel.end_b.id;
                }
                if channel.end_b.pp {
                    pp_bits |= 1 << channel.end_b.id;
                }
            }
        }

        patches.push(SymbolPatch {
            symbol: "microkit_irqs".to_string(),
            data: pd.irq_bits().to_le_bytes().to_vec(),
            expected_size: None,
        });
        patches.push(SymbolPatch {
            symbol: "microkit_notifications".to_string(),
            data: notification_bits.to_le_bytes().to_vec(),
            expected_size: None,
        });
        patches.push(SymbolPatch {
            symbol: "microkit_pps".to_string(),
            data: pp_bits.to_le_bytes().to_vec(),
            expected_size: None,
        });
        patches.push(SymbolPatch {
            symbol: "microkit_ioports".to_string(),
            data: pd.ioport_bits().to_le_bytes().to_vec(),
            expected_size: None,
        });

        // Sanity check that the symbol is of word size so we dont overwrite anything.
        let expected_symbol_size = kernel_config.word_size / 8;
        for setvar in &pd.setvars {
            let data = match &setvar.kind {
                sdf::SysSetVarKind::Size { mr } => mr_name_to_desc[mr].size,
                sdf::SysSetVarKind::Vaddr { address } => *address,
                sdf::SysSetVarKind::Paddr { region } => {
                    mr_name_to_desc[region].paddr().unwrap_or_default()
                }
                sdf::SysSetVarKind::Id { id } => *id,
                sdf::SysSetVarKind::X86IoPortAddr { address } => *address,
                sdf::SysSetVarKind::PrefillSize { mr } => {
                    mr_name_to_desc[mr].prefill_bytes.as_ref().unwrap().len() as u64
                }
            };
            patches.push(SymbolPatch {
                symbol: setvar.symbol.clone(),
                data: data.to_le_bytes().to_vec(),
                expected_size: Some(expected_symbol_size),
            });
        }

        // When the sdf contains 'sym_emit="true"'
        if pd.sym_emit {
            bundles.push(SymbolBundle {
                pd_name: pd.name.clone(),
                patches: patches.clone(),
            });
        }

        let Some(program_image) = &pd.program_image else {
            continue;
        };
        let elf_obj = &mut pd_elf_files[pd_global_idx];

        for patch in &patches {
            // Check that the (setvar) symbol exists in the ELF
            if let Some(expected_symbol_size) = patch.expected_size {
                match elf_obj.find_symbol(&patch.symbol) {
                    Ok((_, symbol_size)) => {
                        if symbol_size != expected_symbol_size {
                            return Err(format!(
                                "setvar to non-word size symbol '{}' for PD '{}', symbol has size '{}' bytes, expected size '{}' bytes",
                                patch.symbol, pd.name, symbol_size, expected_symbol_size
                            ));
                        }
                    }
                    Err(err) => {
                        return Err(format!(
                            "could not patch symbol '{}' in program image for PD '{}' ({}): {}",
                            patch.symbol,
                            pd.name,
                            program_image.display(),
                            err
                        ));
                    }
                }
            }
            elf_obj.write_symbol(&patch.symbol, &patch.data).unwrap();
        }
    }

    Ok(bundles)
}

pub fn write_symbol_bundles(output_dir: &Path, bundles: &[SymbolBundle]) -> Result<(), String> {
    if bundles.is_empty() {
        return Ok(());
    }

    fs::create_dir_all(output_dir).map_err(|err| {
        format!(
            "could not create symbols output directory '{}': {err}",
            output_dir.display()
        )
    })?;

    for bundle in bundles {
        let path = output_dir.join(format!("{}.mktsym", bundle.pd_name));
        fs::write(&path, bundle.serialise()?)
            .map_err(|err| format!("could not write symbol bundle '{}': {err}", path.display()))?;
    }

    Ok(())
}
