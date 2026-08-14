//
// Copyright 2026, UNSW
//
// SPDX-License-Identifier: BSD-2-Clause
//

use std::{fs, path::Path};

use crate::{
    capdl::builder::{
        DLG_MR_CAP, DLG_IOPORT_CAP, PD_BASE_OUTPUT_ENDPOINT_CAP,
        PD_BASE_OUTPUT_NOTIFICATION_CAP, PD_ROOT_CAP_SLOT_RSVD_START,
    },
    sdf::SystemDescription,
};

const DLG_MAGIC: &[u8; 8] = b"CapDelg\0";

const RESOURCE_CHANNEL_NOTIFY: u8 = 1;
const RESOURCE_CHANNEL_PPC: u8 = 2;
const RESOURCE_MEMORY_REGION: u8 = 3;
const RESOURCE_IOPORT: u8 = 4;

struct Resource {
    kind: u8,
    flags: u8,
    slot: u16,
    cap_count: u16,
    arg0: u64,
    arg1: u64,
}

fn put_u16(buf: &mut Vec<u8>, value: u16) {
    buf.extend_from_slice(&value.to_le_bytes());
}

fn put_u32(buf: &mut Vec<u8>, value: u32) {
    buf.extend_from_slice(&value.to_le_bytes());
}

fn put_u64(buf: &mut Vec<u8>, value: u64) {
    buf.extend_from_slice(&value.to_le_bytes());
}

fn resource_push(buf: &mut Vec<u8>, resource: &Resource) {
    //
    // Resources:
    //   u8  kind
    //   u8  flags
    //   u16 slot
    //   u16 cap_count
    //   u64 arg0
    //   u64 arg1
    //
    buf.push(resource.kind);
    buf.push(resource.flags);
    put_u16(buf, resource.slot);
    put_u16(buf, resource.cap_count);
    put_u64(buf, resource.arg0);
    put_u64(buf, resource.arg1);
}

fn build_bundle(system: &SystemDescription, delegatee_idx: usize) -> Result<Vec<u8>, String> {
    let delegators: Vec<_> = system
        .protection_domains
        .iter()
        .enumerate()
        .filter(|(_, pd)| pd.parent == Some(delegatee_idx) && pd.allow_delegation)
        .collect();

    let mut buf = Vec::new();
    //
    // Step.1. fill in headers for the dlg file
    //
    buf.extend_from_slice(DLG_MAGIC);
    put_u32(&mut buf, delegators.len() as u32);
    // total size of the entire pd_name.dlg file
    put_u32(&mut buf, 0); /* placeholder, resolve later */

    //
    // Step.2. handle substructure of the dlg file for each delegator
    //
    for (delegator_offset, (delegator_idx, delegator)) in delegators.into_iter().enumerate() {
        //
        // Step.2.1., generate metadata for all delegated resources of this delegator
        //
        let mut resources = Vec::new();

        // Delegated memory-region mappings.
        let mut next_mr_cap = DLG_MR_CAP as u16; /* 512 max */

        for map in delegator.maps.iter().filter(|map| map.delegated) {
            let mr = system
                .memory_regions
                .iter()
                .find(|mr| mr.name == map.mr)
                .expect("map MR should have been validated");

            // each mr can have up to u64 page count, which is way too many for delegation
            // however, here we can assume the page count is very small, as large counts
            // could have been rejected by the CapDL spec builder during the memory management
            // simulation process...
            let cap_count: u16 = mr
                .page_count
                .try_into()
                .map_err(|_| "delegated MR contains too many pages".to_string())?;

            // sdf seem to have not take more VM attributes than 'cached'
            // so, flags here contains both 'rights' and 'cached' for mappings
            // and we can assume that they are sufficient for a mapping syscall...
            let flags = (map.perms & 0x7) | if map.cached { 1 << 3 } else { 0 };

            resources.push(Resource {
                kind: RESOURCE_MEMORY_REGION,
                flags,             /* mapping rights and (partial) attributes */
                slot: next_mr_cap, /* begins here */
                cap_count,         /* number of slots */
                arg0: map.vaddr,
                arg1: mr.page_size_bytes(),
            });

            // we assume spec builder has checked the slot boundary for us
            next_mr_cap += cap_count;
        }

        // Delegated x86 I/O ports.
        for ioport in delegator.ioports.iter().filter(|ioport| ioport.delegated) {
            resources.push(Resource {
                kind: RESOURCE_IOPORT,
                flags: 0,
                slot: (DLG_IOPORT_CAP + ioport.id) as u16,
                cap_count: 1,
                arg0: ioport.addr,
                arg1: ioport.size,
            });
        }

        // Delegated channel-end caps.
        for channel in &system.channels {
            let end = if channel.end_a.pd == delegator_idx {
                &channel.end_a
            } else if channel.end_b.pd == delegator_idx {
                &channel.end_b
            } else {
                continue;
            };

            if !end.delegated {
                continue;
            }

            if end.notify {
                resources.push(Resource {
                    kind: RESOURCE_CHANNEL_NOTIFY,
                    flags: 0,
                    slot: (PD_BASE_OUTPUT_NOTIFICATION_CAP + end.id) as u16,
                    cap_count: 1,
                    arg0: end.id,
                    arg1: 0,
                });
            }

            if end.pp {
                resources.push(Resource {
                    kind: RESOURCE_CHANNEL_PPC,
                    flags: 0,
                    slot: (PD_BASE_OUTPUT_ENDPOINT_CAP + end.id) as u16,
                    cap_count: 1,
                    arg0: end.id,
                    arg1: 0,
                });
            }
        }
        resources.sort_by_key(|resource| resource.slot);

        //
        // Step.2.2., fill in header for this delegator
        //
        let record_start = buf.len();
        //
        // delegator header:
        //
        //   u16 record_size
        //   u16 resource_count
        //   u32 delegation_cap
        //   u64 pd_id (not internal global_pd_idx)
        //
        // Followed by the resource records.
        //
        put_u16(&mut buf, 0); /* placeholder, resolve later */
        put_u16(&mut buf, resources.len() as u16);
        put_u32(
            &mut buf,
            PD_ROOT_CAP_SLOT_RSVD_START + delegator_offset as u32,
        );
        put_u64(
            &mut buf,
            delegator.id.expect("delegator must be a child/template PD"),
        );
        // The delegated resources begin here...
        for resource in &resources {
            resource_push(&mut buf, resource);
        }

        let record_size: u16 = (buf.len() - record_start)
            .try_into()
            .map_err(|_| "delegator record is too large".to_string())?;

        // fill in the record size in delegator header...
        buf[record_start..record_start + 2].copy_from_slice(&record_size.to_le_bytes());
    }

    // fill in the total size in dlg header
    let total_size = buf.len() as u32;
    buf[12..16].copy_from_slice(&total_size.to_le_bytes());

    Ok(buf)
}

pub(crate) fn write_delegation_bundles(
    system: &SystemDescription,
    output_dir: &Path,
) -> Result<(), String> {
    if output_dir.exists() {
        fs::remove_dir_all(output_dir).map_err(|err| {
            format!(
                "failed to clean delegation output directory '{}': {err}",
                output_dir.display()
            )
        })?;
    }
    fs::create_dir_all(output_dir).map_err(|err| {
        format!(
            "failed to create delegation output directory '{}': {err}",
            output_dir.display()
        )
    })?;

    for (pd_idx, pd) in system
        .protection_domains
        .iter()
        .enumerate()
        .filter(|(_, pd)| pd.delegatee)
    {
        let bundle = build_bundle(system, pd_idx)?;
        let path = output_dir.join(format!("{}.dlg", pd.name));

        fs::write(&path, bundle).map_err(|err| {
            format!(
                "failed to write delegation bundle '{}': {err}",
                path.display()
            )
        })?;
    }

    Ok(())
}
