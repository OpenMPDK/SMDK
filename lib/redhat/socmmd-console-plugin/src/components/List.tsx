// // @ts-nocheck
import * as React from 'react';
import "./common.css";
import { useTranslation } from 'react-i18next';
import { sortable } from '@patternfly/react-table';
import { ExclamationTriangleIcon, ExclamationCircleIcon }
  from '@patternfly/react-icons/dist/esm/icons';
import { Divider, Alert, AlertGroup, Button, Form, FormGroup, Grid, GridItem, Icon, HelperText, HelperTextItem, Title, Chip }
  from '@patternfly/react-core';
import { useActiveNamespace, useLabelsModal, k8sCreate, useK8sModel, getGroupVersionKindForResource, useK8sWatchResource, K8sResourceCommon, NodeKind, WatchK8sResource, ObjectMetadata, RowProps, TableData, TableColumn, VirtualizedTable, useActiveColumns }
  from '@openshift-console/dynamic-plugin-sdk';
import { useHistory }
  from 'react-router-dom';
// Resource Type
declare type Resource = {
  apiVersion: string,
  group: string,
  version: string,
  kind: string,
  metadata?: ObjectMetadata,
}

// Node Item Type
declare type NodeResponse = {
  metadata: ObjectMetadata
} & NodeKind;

// Node Status Type
declare type Node = {
  name?: string,
  type?: string,
  status?: string,
  reason?: string,
  message?: string,
  lastTransitionTime?: string,
  lastHeartbeatTime?: string,
}

// Item Table Props
type ItemTableProps = {
  data: Node[];
  unfilteredData: Node[];
  loaded: boolean;
  loadError: any;
};

const K8sAPIConsumer: React.FC = () => {
  const [activeNamespace] = useActiveNamespace();
  console.log("namespace:", activeNamespace);

  // constants
  const GROUP = 'cmmd.samsung.com';
  const VERSION = 'v1';
  const KIND = "Label";
  const NAME = "cmmd-node-label";
  const NAMESPACE = activeNamespace;

  // label resource template
  const labelResParam: Resource = {
    apiVersion: `${GROUP}/${VERSION}`,
    group: GROUP,
    version: VERSION,
    kind: KIND,
    metadata: {
      name: NAME,
      namespace: NAMESPACE,
    },
  };

  // node resource param
  const nodeResParam: Resource = {
    apiVersion: "core/V1",
    group: "core",
    version: "v1",
    kind: "Node",
  }

  const { t } = useTranslation('plugin__socmmd-console-plugin');
  const [errData, setErrData] = React.useState<string>("");
  const [labelModel] = useK8sModel(labelResParam);
  const [labelsStr, setLabelsStr] = React.useState<string[]>([]);
  const [labelRes, setLabelRes] = React.useState<Resource>(labelResParam);
  const history = useHistory();
  let nodes: Node[] = [];
  let waitCreating = false;

  // create
  const create = () => {
    if (waitCreating) {
      return;
    }
    waitCreating = true;
    k8sCreate({ model: labelModel, data: labelResParam })
      .then(() => {
        setErrData('');
      })
      .catch((e) => {
        console.error('create error', e);
        console.error(JSON.stringify(e));
        if (e.code == 409) {
          console.error(JSON.stringify(e));
          return
        }
        setErrData(e.message);
      }).finally(() => {
        waitCreating = false;
      });
  };

  // cancel button
  const SetCancelButton = () => {
    /*
    /k8s/ns/{namespace}/clusterserviceversions/{operator.v0.0.1}/{my.package.com}~v1~Label/~new
    to
    /k8s/ns/{namespace}/operators.coreos.com~v1alpha1~ClusterServiceVersion/{operator.v0.0.1}
    */
    const arrPath = location.pathname.split("/");
    const operHome = "/" + [arrPath[1], arrPath[2], arrPath[3], "operators.coreos.com~v1alpha1~ClusterServiceVersion", arrPath[5]].join("/");
    const currPath = arrPath[arrPath.length - 1];
    if (currPath.indexOf("~new") >= 0) {
      return <Button variant="secondary" className='pf-u-mr-sm' onClick={() => history.push(operHome)}>{t('Back')}</Button>
    } 
    return <></>
  }

  // label button
  const SetLabelButton = ({ labelRes: param }) => {
    const launchModal = useLabelsModal(param);
    return <Button onClick={launchModal} variant="primary" sizes="sm">{t('Set Label')}</Button>
  }

  // watch labels
  const watchLabelRes: WatchK8sResource = {
    groupVersionKind: getGroupVersionKindForResource(labelResParam),
    name: NAME,
    namespace: NAMESPACE,
  };
  const [labelData, labelLoaded, labelError]: [K8sResourceCommon, boolean, any] = useK8sWatchResource(watchLabelRes);
  console.log('label watch ->', labelData, labelLoaded, labelError);
  if (labelLoaded) {
    const oldLabels = labelRes.metadata.labels || {};
    const newLabels = labelData.metadata.labels || {};

    let changed = false;
    Object.keys(oldLabels).forEach(k => {
      if (oldLabels[k] != newLabels[k]) {
        console.log('old labels modified');
        changed = true;
      }
    });
    Object.keys(newLabels).forEach(k => {
      if (oldLabels[k] != newLabels[k]) {
        console.log('new labels modified');
        changed = true;
      }
    });

    if (changed) {
      labelRes.metadata.labels = newLabels;
      setLabelsStr(Object.keys(newLabels).map((k) => `${k}=${newLabels[k]}`));
      setLabelRes(labelRes);
    }

  } else {
    // not loaded
    if (labelError) {
      if (labelError.code == 404) {
        create();
      } else {
        console.error('load error', JSON.stringify(labelError, null, 2))
        // setErrData(error.json.message);
      }
    }
  }

  // watch nodes
  const watchNodeRes: WatchK8sResource = {
    groupVersionKind: getGroupVersionKindForResource(nodeResParam),
    isList: true,
  };
  const [nodeData, nodeLoaded, nodeError]: [K8sResourceCommon[], boolean, any] = useK8sWatchResource(watchNodeRes);
  console.log('node watch ->', nodeData, nodeLoaded, nodeError);
  if (nodeLoaded) {
    // console.log('nodeData', nodeData)
    nodes = nodeData.map((node: NodeResponse) => {
      const status = node.status.conditions.filter(v => v.type == "Ready")[0];
      return Object.assign({}, status, { name: node.metadata.name });
    });
  }

  // node status table
  const NodeTable: React.FC<ItemTableProps> = ({ data, unfilteredData, loaded, loadError }) => {
    // columns
    const nodeTableColumns = React.useMemo<TableColumn<Node>[]>(
      () => [
        { title: "Name", id: "name", sort: "name", transforms: [sortable] },
        { title: "Status", id: "status", sort: "status", transforms: [sortable] },
        { title: "Reason", id: "reason", sort: "reason" },
        { title: "Message", id: "message", sort: "message" },
        { title: "Last Transition Time", id: "lastTransitionTime", sort: "lastTransitionTime", transforms: [sortable] },
        { title: "Last Heartbeat Time", id: "lastHeartbeatTime", sort: "lastHeartbeatTime", transforms: [sortable] },
      ], []
    );
    const [columns] = useActiveColumns({ columns: nodeTableColumns, columnManagementID: "", showNamespaceOverride: false });

    // warning icon
    const StatusData: React.FC<Node> = ({ status }) => {
      return (status == "True") ?
        (<><Icon status="info"><ExclamationCircleIcon /></Icon>&nbsp;Ready</>) :
        (<><Icon status="danger"><ExclamationTriangleIcon /></Icon>&nbsp;Not Ready</>)
    }

    // rows
    const NodeRow: React.FC<RowProps<Node>> = ({ obj, activeColumnIDs }) => {
      return (
        <>
          <TableData id={columns[0].id} activeColumnIDs={activeColumnIDs}>{obj.name}</TableData>
          <TableData id={columns[1].id} activeColumnIDs={activeColumnIDs}><StatusData status={obj.status}></StatusData></TableData>
          <TableData id={columns[2].id} activeColumnIDs={activeColumnIDs}>{obj.reason}</TableData>
          <TableData id={columns[3].id} activeColumnIDs={activeColumnIDs}>{obj.message}</TableData>
          <TableData id={columns[4].id} activeColumnIDs={activeColumnIDs}>{obj.lastTransitionTime}</TableData>
          <TableData id={columns[5].id} activeColumnIDs={activeColumnIDs}>{obj.lastHeartbeatTime}</TableData>
        </>
      )
    };
    return (
      <VirtualizedTable<Node>
        data={data}
        unfilteredData={unfilteredData}
        loaded={loaded}
        loadError={loadError}
        columns={columns}
        Row={NodeRow}
      />
    );
  }

  return (
    <Grid className="xl pf-u-m-lg">

      {/* -------------------- title -------------------- */}
      <GridItem span={8}>
        {/* 에러메시지 */}
        
        <Title headingLevel="h1" data-test="test-k8sapi-title">{t('Set Labels for CMMD Nodes')}</Title>
        {errData && (
          <AlertGroup>
            <Alert data-test="test-k8api-error" title={errData} variant="warning" isInline />
          </AlertGroup>
        )}
      </GridItem>
      <GridItem span={4} className='pf-u-text-align-right'>
        {/* 라벨 버튼 */}
        <SetCancelButton></SetCancelButton>
        <SetLabelButton labelRes={labelRes}></SetLabelButton>
      </GridItem>
      {/* -------------------- title -------------------- */}

      <Divider component="div" className='pf-u-my-sm' />

      {/* -------------------- label list -------------------- */}
      <GridItem span={12}>
        {/* 라벨 목록 */}
        <Form>
          <FormGroup label="- Assigned Labels" className='pf-u-mt-sm'>
            <HelperText>
              <HelperTextItem variant="warning" hasIcon>{t('Setting the labels will trigger server reboot.')}</HelperTextItem>
            </HelperText>
            {labelsStr.map((v, i) => (
              <Chip className='operator-label-chip' key="chip4" isReadOnly>{v}</Chip>
            ))}
          </FormGroup>
        </Form>
        {/* 라벨 경고 */}
        {labelsStr.length == 0 &&
          <HelperText>
            <HelperTextItem variant="error" hasIcon>{t('No labels found. To use this Operator you have to set the labels.')}</HelperTextItem>
          </HelperText>
        }
      </GridItem>
      {/* -------------------- label list -------------------- */}

      <Divider component="div" className='pf-u-my-sm' />

      {/* -------------------- node status list -------------------- */}
      <GridItem span={12}>
        {/* 노드 목록 */}
        <Form>
          <FormGroup label="- Node Status" className='pf-u-mt-sm oper-label-height'>
            <NodeTable data={nodes} unfilteredData={nodes} loaded={nodeLoaded} loadError={nodeError}></NodeTable>
          </FormGroup>
        </Form>
      </GridItem>
      {/* -------------------- node status list -------------------- */}

    </Grid>
  );
};

export default K8sAPIConsumer;
